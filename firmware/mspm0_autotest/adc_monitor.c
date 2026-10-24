#include "adc_monitor.h"

#include <limits.h>
#include <math.h>

#include "ti_msp_dl_config.h"

#define ADC_EXC_INST            ADC0
#define ADC_EXC_IOMUX           IOMUX_PINCM60
#define ADC_EXC_CHANNEL         DL_ADC12_INPUT_CHAN_0

#define ADC_SENSE_INST          ADC1
#define ADC_SENSE_IOMUX         IOMUX_PINCM37
#define ADC_SENSE_CHANNEL       DL_ADC12_INPUT_CHAN_0

#define ADC_SAMPLE_COUNT        (4096U)
#define ADC_EXC_DMA_CHANNEL     (0U)
#define ADC_SENSE_DMA_CHANNEL   (1U)
#define ADC_REFERENCE_MV        (3300U)
#define ADC_FULL_SCALE          (4095U)
#define TWO_PI                  (6.28318530717958647692)

static uint16_t excSamples[ADC_SAMPLE_COUNT];
static uint16_t senseSamples[ADC_SAMPLE_COUNT];

static const DL_ADC12_ClockConfig adcClockConfig = {
    .clockSel    = DL_ADC12_CLOCK_ULPCLK,
    .freqRange  = DL_ADC12_CLOCK_FREQ_RANGE_24_TO_32,
    .divideRatio = DL_ADC12_CLOCK_DIVIDE_4
};

static const DL_DMA_Config excDmaConfig = {
    .trigger        = DMA_ADC0_EVT_GEN_BD_TRIG,
    .triggerType    = DL_DMA_TRIGGER_TYPE_EXTERNAL,
    .transferMode   = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode   = DL_DMA_NORMAL_MODE,
    .srcWidth       = DL_DMA_WIDTH_HALF_WORD,
    .destWidth      = DL_DMA_WIDTH_HALF_WORD,
    .srcIncrement   = DL_DMA_ADDR_UNCHANGED,
    .destIncrement  = DL_DMA_ADDR_INCREMENT
};

static const DL_DMA_Config senseDmaConfig = {
    .trigger        = DMA_ADC1_EVT_GEN_BD_TRIG,
    .triggerType    = DL_DMA_TRIGGER_TYPE_EXTERNAL,
    .transferMode   = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode   = DL_DMA_NORMAL_MODE,
    .srcWidth       = DL_DMA_WIDTH_HALF_WORD,
    .destWidth      = DL_DMA_WIDTH_HALF_WORD,
    .srcIncrement   = DL_DMA_ADDR_UNCHANGED,
    .destIncrement  = DL_DMA_ADDR_INCREMENT
};

static uint32_t ADCMonitor_integerSqrt(uint64_t value)
{
    uint64_t bit = (uint64_t) 1U << 62U;
    uint64_t result = 0U;

    while (bit > value) {
        bit >>= 2U;
    }

    while (bit != 0U) {
        if (value >= (result + bit)) {
            value -= result + bit;
            result = (result >> 1U) + bit;
        } else {
            result >>= 1U;
        }
        bit >>= 2U;
    }

    return (uint32_t) result;
}

static uint16_t ADCMonitor_sinePeakToPeak(
    uint64_t sum, uint64_t sumSquares)
{
    const uint64_t count = ADC_SAMPLE_COUNT;
    uint64_t acEnergyNumerator;
    uint32_t scaledRoot;
    uint32_t peakToPeak;

    /*
     * Variance = (sum(x^2) * N - sum(x)^2) / N^2.
     * For a sine wave, Vpp = sqrt(8 * variance). Keeping N^2 outside
     * the square root preserves precision for small signals.
     */
    acEnergyNumerator = (sumSquares * count) - (sum * sum);
    scaledRoot = ADCMonitor_integerSqrt(8U * acEnergyNumerator);
    peakToPeak = (scaledRoot + (ADC_SAMPLE_COUNT / 2U)) / ADC_SAMPLE_COUNT;

    if (peakToPeak > ADC_FULL_SCALE) {
        peakToPeak = ADC_FULL_SCALE;
    }
    return (uint16_t) peakToPeak;
}

static void ADCMonitor_initInstance(ADC12_Regs *adc, uint32_t channel)
{
    DL_ADC12_reset(adc);
    DL_ADC12_enablePower(adc);
    delay_cycles(POWER_STARTUP_DELAY);

    DL_ADC12_setClockConfig(adc, &adcClockConfig);
    DL_ADC12_initSingleSample(adc,
        DL_ADC12_REPEAT_MODE_ENABLED,
        DL_ADC12_SAMPLING_SOURCE_AUTO,
        DL_ADC12_TRIG_SRC_SOFTWARE,
        DL_ADC12_SAMP_CONV_RES_12_BIT,
        DL_ADC12_SAMP_CONV_DATA_FORMAT_UNSIGNED);
    DL_ADC12_setPowerDownMode(adc, DL_ADC12_POWER_DOWN_MODE_MANUAL);
    /*
     * DIV4 gives enough samples per cycle at high frequency while extending
     * the capture window to include multiple cycles at 1 kHz.
     */
    DL_ADC12_setSampleTime0(adc, 4U);
    DL_ADC12_configConversionMem(adc,
        DL_ADC12_MEM_IDX_0,
        channel,
        DL_ADC12_REFERENCE_VOLTAGE_VDDA,
        DL_ADC12_SAMPLE_TIMER_SOURCE_SCOMP0,
        DL_ADC12_AVERAGING_MODE_DISABLED,
        DL_ADC12_BURN_OUT_SOURCE_DISABLED,
        DL_ADC12_TRIGGER_MODE_AUTO_NEXT,
        DL_ADC12_WINDOWS_COMP_MODE_DISABLED);
    DL_ADC12_enableDMA(adc);
    DL_ADC12_enableDMATrigger(adc, DL_ADC12_DMA_MEM0_RESULT_LOADED);
    DL_ADC12_enableConversions(adc);
}

static void ADCMonitor_initDma(void)
{
    DL_DMA_initChannel(DMA, ADC_EXC_DMA_CHANNEL, &excDmaConfig);
    DL_DMA_initChannel(DMA, ADC_SENSE_DMA_CHANNEL, &senseDmaConfig);
    DL_DMA_enableRoundRobinPriority(DMA);
}

static bool ADCMonitor_capture(void)
{
    uint32_t timeout = CPUCLK_FREQ;

    DL_ADC12_stopConversion(ADC_EXC_INST);
    DL_ADC12_stopConversion(ADC_SENSE_INST);
    DL_DMA_disableChannel(DMA, ADC_EXC_DMA_CHANNEL);
    DL_DMA_disableChannel(DMA, ADC_SENSE_DMA_CHANNEL);

    DL_DMA_setSrcAddr(DMA, ADC_EXC_DMA_CHANNEL,
        DL_ADC12_getMemResultAddress(ADC_EXC_INST, DL_ADC12_MEM_IDX_0));
    DL_DMA_setDestAddr(DMA, ADC_EXC_DMA_CHANNEL, (uint32_t) excSamples);
    DL_DMA_setTransferSize(DMA, ADC_EXC_DMA_CHANNEL, ADC_SAMPLE_COUNT);

    DL_DMA_setSrcAddr(DMA, ADC_SENSE_DMA_CHANNEL,
        DL_ADC12_getMemResultAddress(ADC_SENSE_INST, DL_ADC12_MEM_IDX_0));
    DL_DMA_setDestAddr(DMA, ADC_SENSE_DMA_CHANNEL, (uint32_t) senseSamples);
    DL_DMA_setTransferSize(DMA, ADC_SENSE_DMA_CHANNEL, ADC_SAMPLE_COUNT);

    DL_DMA_clearInterruptStatus(DMA,
        DL_DMA_INTERRUPT_CHANNEL0 | DL_DMA_INTERRUPT_CHANNEL1);
    DL_DMA_enableChannel(DMA, ADC_EXC_DMA_CHANNEL);
    DL_DMA_enableChannel(DMA, ADC_SENSE_DMA_CHANNEL);

    DL_ADC12_enableConversions(ADC_EXC_INST);
    DL_ADC12_enableConversions(ADC_SENSE_INST);
    DL_ADC12_startConversion(ADC_EXC_INST);
    DL_ADC12_startConversion(ADC_SENSE_INST);

    while (((DL_DMA_getTransferSize(DMA, ADC_EXC_DMA_CHANNEL) != 0U) ||
            (DL_DMA_getTransferSize(DMA, ADC_SENSE_DMA_CHANNEL) != 0U)) &&
           (timeout != 0U)) {
        timeout--;
    }

    DL_ADC12_stopConversion(ADC_EXC_INST);
    DL_ADC12_stopConversion(ADC_SENSE_INST);
    DL_DMA_disableChannel(DMA, ADC_EXC_DMA_CHANNEL);
    DL_DMA_disableChannel(DMA, ADC_SENSE_DMA_CHANNEL);
    return (timeout != 0U);
}

static void ADCMonitor_clearResult(ADCMonitorResult *result)
{
    result->excAverage = 0U;
    result->senseAverage = 0U;
    result->excPeakToPeak = 0U;
    result->sensePeakToPeak = 0U;
    result->impedanceRealMilliOhm = 0;
    result->impedanceImagMilliOhm = 0;
    result->lossPpm = 0;
    result->capacitancePf = 0U;
    result->samplesPerCycleX100 = 0U;
    result->impedanceValid = 0U;
}

static double ADCMonitor_estimateSamplesPerCycle(
    double excMean, uint16_t excPeakToPeak)
{
    double previous = (double) excSamples[0] - excMean;
    double lastCrossing = -1.0;
    double intervalSum = 0.0;
    uint32_t intervalCount = 0U;
    uint32_t index;

    if (excPeakToPeak < 12U) {
        return 0.0;
    }

    for (index = 1U; index < ADC_SAMPLE_COUNT; index++) {
        double current = (double) excSamples[index] - excMean;

        if (((previous <= 0.0) && (current > 0.0)) ||
            ((previous >= 0.0) && (current < 0.0))) {
            double denominator = current - previous;
            double crossing = (double) (index - 1U);

            if (denominator != 0.0) {
                crossing += -previous / denominator;
            }
            if (lastCrossing >= 0.0) {
                intervalSum += crossing - lastCrossing;
                intervalCount++;
            }
            lastCrossing = crossing;
        }
        previous = current;
    }

    if (intervalCount == 0U) {
        return 0.0;
    }
    return 2.0 * intervalSum / (double) intervalCount;
}

static int32_t ADCMonitor_roundToInt32(double value)
{
    if (value > (double) INT32_MAX) {
        return INT32_MAX;
    }
    if (value < (double) INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t) ((value >= 0.0) ? (value + 0.5) : (value - 0.5));
}

static uint32_t ADCMonitor_roundToUint32(double value)
{
    if (value <= 0.0) {
        return 0U;
    }
    if (value > (double) UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t) (value + 0.5);
}

static void ADCMonitor_calculateImpedance(ADCMonitorResult *result,
    uint32_t frequencyHz, uint32_t referenceMilliOhms,
    double excMean, double senseMean)
{
    double samplesPerCycle;
    double omega;
    double stepCos;
    double stepSin;
    double oscillatorCos = 1.0;
    double oscillatorSin = 0.0;
    double sumCos = 0.0;
    double sumSin = 0.0;
    double sumCosSquared = 0.0;
    double sumSinSquared = 0.0;
    double sumCosSin = 0.0;
    double excCos = 0.0;
    double excSin = 0.0;
    double senseCos = 0.0;
    double senseSin = 0.0;
    double centeredCosSquared;
    double centeredSinSquared;
    double centeredCosSin;
    double determinant;
    double excReal;
    double excImag;
    double senseReal;
    double senseImag;
    double senseMagnitudeSquared;
    double ratioReal;
    double ratioImag;
    double referenceOhms;
    double zReal;
    double zImag;
    uint32_t index;

    if ((frequencyHz == 0U) || (referenceMilliOhms == 0U)) {
        return;
    }

    samplesPerCycle = ADCMonitor_estimateSamplesPerCycle(
        excMean, result->excPeakToPeak);
    if ((samplesPerCycle < 4.0) ||
        (samplesPerCycle > (2.0 * (double) ADC_SAMPLE_COUNT))) {
        return;
    }

    omega = TWO_PI / samplesPerCycle;
    stepCos = cos(omega);
    stepSin = sin(omega);

    for (index = 0U; index < ADC_SAMPLE_COUNT; index++) {
        double exc = (double) excSamples[index];
        double sense = (double) senseSamples[index];
        double nextCos;

        sumCos += oscillatorCos;
        sumSin += oscillatorSin;
        sumCosSquared += oscillatorCos * oscillatorCos;
        sumSinSquared += oscillatorSin * oscillatorSin;
        sumCosSin += oscillatorCos * oscillatorSin;
        excCos += exc * oscillatorCos;
        excSin += exc * oscillatorSin;
        senseCos += sense * oscillatorCos;
        senseSin += sense * oscillatorSin;

        nextCos = (oscillatorCos * stepCos) -
            (oscillatorSin * stepSin);
        oscillatorSin = (oscillatorSin * stepCos) +
            (oscillatorCos * stepSin);
        oscillatorCos = nextCos;
    }

    /*
     * Fit y = DC + A*cos(w*n) + B*sin(w*n). Removing the fitted DC and
     * solving the 2x2 normal equations avoids leakage when the capture does
     * not contain an integer number of periods.
     */
    centeredCosSquared = sumCosSquared -
        ((sumCos * sumCos) / ADC_SAMPLE_COUNT);
    centeredSinSquared = sumSinSquared -
        ((sumSin * sumSin) / ADC_SAMPLE_COUNT);
    centeredCosSin = sumCosSin -
        ((sumCos * sumSin) / ADC_SAMPLE_COUNT);
    determinant = (centeredCosSquared * centeredSinSquared) -
        (centeredCosSin * centeredCosSin);
    if (fabs(determinant) < 1.0e-9) {
        return;
    }

    excCos -= excMean * sumCos;
    excSin -= excMean * sumSin;
    senseCos -= senseMean * sumCos;
    senseSin -= senseMean * sumSin;

    excReal = ((excCos * centeredSinSquared) -
        (excSin * centeredCosSin)) / determinant;
    excImag = -(((excSin * centeredCosSquared) -
        (excCos * centeredCosSin)) / determinant);
    senseReal = ((senseCos * centeredSinSquared) -
        (senseSin * centeredCosSin)) / determinant;
    senseImag = -(((senseSin * centeredCosSquared) -
        (senseCos * centeredCosSin)) / determinant);

    senseMagnitudeSquared =
        (senseReal * senseReal) + (senseImag * senseImag);
    if (senseMagnitudeSquared < 1.0) {
        return;
    }

    ratioReal = ((excReal * senseReal) +
        (excImag * senseImag)) / senseMagnitudeSquared;
    ratioImag = ((excImag * senseReal) -
        (excReal * senseImag)) / senseMagnitudeSquared;
    referenceOhms = (double) referenceMilliOhms / 1000.0;
    zReal = referenceOhms * (ratioReal - 1.0);
    zImag = referenceOhms * ratioImag;

    result->impedanceRealMilliOhm =
        ADCMonitor_roundToInt32(zReal * 1000.0);
    result->impedanceImagMilliOhm =
        ADCMonitor_roundToInt32(zImag * 1000.0);
    result->samplesPerCycleX100 =
        ADCMonitor_roundToUint32(samplesPerCycle * 100.0);

    if (zImag < -0.000001) {
        double reactance = -zImag;
        double capacitancePf =
            1.0e12 / (TWO_PI * (double) frequencyHz * reactance);
        double lossPpm = (zReal / reactance) * 1.0e6;

        result->capacitancePf =
            ADCMonitor_roundToUint32(capacitancePf);
        result->lossPpm = ADCMonitor_roundToInt32(lossPpm);
        result->impedanceValid = 1U;
    }
}

void ADCMonitor_init(void)
{
    DL_GPIO_initPeripheralAnalogFunction(ADC_EXC_IOMUX);
    DL_GPIO_initPeripheralAnalogFunction(ADC_SENSE_IOMUX);

    ADCMonitor_initInstance(ADC_EXC_INST, ADC_EXC_CHANNEL);
    ADCMonitor_initInstance(ADC_SENSE_INST, ADC_SENSE_CHANNEL);
    ADCMonitor_initDma();
}

void ADCMonitor_measure(ADCMonitorResult *result,
    uint32_t frequencyHz, uint32_t referenceMilliOhms)
{
    uint64_t excSum = 0U;
    uint64_t senseSum = 0U;
    uint64_t excSumSquares = 0U;
    uint64_t senseSumSquares = 0U;
    uint32_t index;

    ADCMonitor_clearResult(result);
    if (!ADCMonitor_capture()) {
        return;
    }

    for (index = 0U; index < ADC_SAMPLE_COUNT; index++) {
        uint16_t exc = excSamples[index];
        uint16_t sense = senseSamples[index];

        excSum += exc;
        senseSum += sense;
        excSumSquares += (uint32_t) exc * (uint32_t) exc;
        senseSumSquares += (uint32_t) sense * (uint32_t) sense;
    }

    result->excAverage = (uint16_t) (excSum / ADC_SAMPLE_COUNT);
    result->senseAverage = (uint16_t) (senseSum / ADC_SAMPLE_COUNT);
    result->excPeakToPeak =
        ADCMonitor_sinePeakToPeak(excSum, excSumSquares);
    result->sensePeakToPeak =
        ADCMonitor_sinePeakToPeak(senseSum, senseSumSquares);
    ADCMonitor_calculateImpedance(result,
        frequencyHz, referenceMilliOhms,
        (double) excSum / ADC_SAMPLE_COUNT,
        (double) senseSum / ADC_SAMPLE_COUNT);
}

uint32_t ADCMonitor_rawToMillivolts(uint16_t raw)
{
    return ((((uint32_t) raw) * ADC_REFERENCE_MV) +
        (ADC_FULL_SCALE / 2U)) / ADC_FULL_SCALE;
}
