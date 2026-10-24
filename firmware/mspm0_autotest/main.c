#include <stdint.h>
#include "adc_monitor.h"
#include "ad9833.h"
#include "range_control.h"
#include "ti_msp_dl_config.h"

#define BOARD_LED_PIN DL_GPIO_PIN_22
#define BOARD_LED_IOMUX IOMUX_PINCM50
#define AD9833_OUTPUT_HZ (1000U)
#define COMMAND_BUFFER_SIZE (24U)
#define MIN_OUTPUT_HZ (1U)
#define MAX_OUTPUT_HZ (100000U)
#define MONITOR_INTERVAL_MS (500U)
#define AUTO_RATIO_LOW_PERMILLE (80U)
#define AUTO_RATIO_HIGH_PERMILLE (920U)
#define AUTO_RECHECK_DELAY_REPORTS (20U)

static uint8_t monitorEnabled = 1U;
static uint8_t autoRangeEnabled = 0U;
static uint8_t autoRecheckDelay = 0U;
static uint32_t currentFrequencyHz = AD9833_OUTPUT_HZ;

static void uart_write(const char *text)
{
    while (*text != '\0') {
        DL_UART_Main_transmitDataBlocking(UART_0_INST, (uint8_t) *text);
        text++;
    }
}

static void uart_write_uint32(uint32_t value)
{
    char digits[10];
    uint32_t count = 0U;

    if (value == 0U) {
        DL_UART_Main_transmitDataBlocking(UART_0_INST, (uint8_t) '0');
        return;
    }

    while ((value != 0U) && (count < sizeof(digits))) {
        digits[count] = (char) ('0' + (value % 10U));
        value /= 10U;
        count++;
    }

    while (count != 0U) {
        count--;
        DL_UART_Main_transmitDataBlocking(
            UART_0_INST, (uint8_t) digits[count]);
    }
}

static void uart_write_int32(int32_t value)
{
    if (value < 0) {
        DL_UART_Main_transmitDataBlocking(UART_0_INST, (uint8_t) '-');
        uart_write_uint32((uint32_t) (-(int64_t) value));
    } else {
        uart_write_uint32((uint32_t) value);
    }
}

static void uart_write_fraction6(int32_t valuePpm)
{
    uint32_t magnitude;
    uint32_t divisor;

    if (valuePpm < 0) {
        DL_UART_Main_transmitDataBlocking(UART_0_INST, (uint8_t) '-');
        magnitude = (uint32_t) (-(int64_t) valuePpm);
    } else {
        magnitude = (uint32_t) valuePpm;
    }

    uart_write_uint32(magnitude / 1000000U);
    DL_UART_Main_transmitDataBlocking(UART_0_INST, (uint8_t) '.');
    divisor = 100000U;
    while (divisor != 0U) {
        DL_UART_Main_transmitDataBlocking(UART_0_INST,
            (uint8_t) ('0' + ((magnitude / divisor) % 10U)));
        divisor /= 10U;
    }
}

static uint32_t range_reference_milliohms(RangeSelection range)
{
    switch (range) {
        case RANGE_4R7:
            return 4700U;
        case RANGE_47R:
            return 47000U;
        case RANGE_470R:
            return 470000U;
        case RANGE_4K7:
            return 4700000U;
        case RANGE_ALL_OFF:
        default:
            return 0U;
    }
}

static void measure_active_range(ADCMonitorResult *result)
{
    RangeSelection range = RangeControl_getActive();

    ADCMonitor_measure(result, currentFrequencyHz,
        range_reference_milliohms(range));
}

static void uart_show_help(void)
{
    uart_write(
        "\r\nCommands:\r\n"
        "  F1000     Set sine frequency to 1000 Hz\r\n"
        "  F1..F100000 are supported\r\n"
        "  R0        Turn all range relays off\r\n"
        "  R1        Select 4.7 ohm range (PA8)\r\n"
        "  R2        Select 47 ohm range (PA9)\r\n"
        "  R3        Select 470 ohm range (PB4)\r\n"
        "  R4        Select 4.7 kohm range (PB5)\r\n"
        "  A1        Enable auto range using R1 and R3\r\n"
        "  A0        Disable auto range, keep current range\r\n"
        "  M0        Stop periodic ADC reports\r\n"
        "  M1        Start periodic ADC reports\r\n"
        "  ?         Show this help\r\n");
}

static const char *range_name(RangeSelection range)
{
    switch (range) {
        case RANGE_4R7:
            return "4.7 ohm (R1)";
        case RANGE_47R:
            return "47 ohm (R2)";
        case RANGE_470R:
            return "470 ohm (R3)";
        case RANGE_4K7:
            return "4.7 kohm (R4)";
        case RANGE_ALL_OFF:
        default:
            return "all off";
    }
}

static uint32_t amplitude_ratio_permille(const ADCMonitorResult *result)
{
    uint32_t ratio;

    if (result->excPeakToPeak < 4U) {
        return 0U;
    }

    ratio = (((uint32_t) result->sensePeakToPeak * 1000U) +
        (result->excPeakToPeak / 2U)) / result->excPeakToPeak;
    if (ratio > 1000U) {
        ratio = 1000U;
    }
    return ratio;
}

static uint32_t auto_range_quality(const ADCMonitorResult *result)
{
    uint32_t ratio = amplitude_ratio_permille(result);

    return (ratio <= 500U) ? ratio : (1000U - ratio);
}

static void auto_select_range(void)
{
    ADCMonitorResult resultR1;
    ADCMonitorResult resultR3;
    uint32_t ratioR1;
    uint32_t ratioR3;
    RangeSelection selected;

    RangeControl_select(RANGE_4R7);
    ADCMonitor_measure(&resultR1, currentFrequencyHz,
        range_reference_milliohms(RANGE_4R7));

    RangeControl_select(RANGE_470R);
    ADCMonitor_measure(&resultR3, currentFrequencyHz,
        range_reference_milliohms(RANGE_470R));

    ratioR1 = amplitude_ratio_permille(&resultR1);
    ratioR3 = amplitude_ratio_permille(&resultR3);
    selected = (auto_range_quality(&resultR1) >
                    auto_range_quality(&resultR3)) ?
        RANGE_4R7 : RANGE_470R;

    if (selected != RANGE_470R) {
        RangeControl_select(selected);
    }

    autoRecheckDelay = AUTO_RECHECK_DELAY_REPORTS;
    uart_write("AUTO: R1_ratio=");
    uart_write_uint32(ratioR1);
    uart_write("/1000,R3_ratio=");
    uart_write_uint32(ratioR3);
    uart_write("/1000,selected=");
    uart_write(range_name(selected));
    uart_write("\r\n");
}

static void uart_report_adc(void)
{
    ADCMonitorResult result;
    uint32_t ratio;

    measure_active_range(&result);
    ratio = amplitude_ratio_permille(&result);

    if (autoRangeEnabled != 0U) {
        if (autoRecheckDelay != 0U) {
            autoRecheckDelay--;
        } else if ((ratio < AUTO_RATIO_LOW_PERMILLE) ||
                   (ratio > AUTO_RATIO_HIGH_PERMILLE) ||
                   (RangeControl_getActive() == RANGE_ALL_OFF)) {
            auto_select_range();
            measure_active_range(&result);
        }
    }

    uart_write("exc_dc_mV:");
    uart_write_uint32(ADCMonitor_rawToMillivolts(result.excAverage));
    uart_write(",sense_dc_mV:");
    uart_write_uint32(ADCMonitor_rawToMillivolts(result.senseAverage));
    uart_write(",exc_pp_mV:");
    uart_write_uint32(ADCMonitor_rawToMillivolts(result.excPeakToPeak));
    uart_write(",sense_pp_mV:");
    uart_write_uint32(ADCMonitor_rawToMillivolts(result.sensePeakToPeak));
    if (result.impedanceValid != 0U) {
        uart_write(",C_nF:");
        uart_write_uint32(result.capacitancePf / 1000U);
        DL_UART_Main_transmitDataBlocking(UART_0_INST, (uint8_t) '.');
        uart_write_uint32((result.capacitancePf / 100U) % 10U);
        uart_write_uint32((result.capacitancePf / 10U) % 10U);
        uart_write_uint32(result.capacitancePf % 10U);
        uart_write(",D:");
        uart_write_fraction6(result.lossPpm);
        uart_write(",Z_re_mohm:");
        uart_write_int32(result.impedanceRealMilliOhm);
        uart_write(",Z_im_mohm:");
        uart_write_int32(result.impedanceImagMilliOhm);
        uart_write(",spc_x100:");
        uart_write_uint32(result.samplesPerCycleX100);
    } else {
        uart_write(",C_nF:NA,D:NA,Z_re_mohm:NA,Z_im_mohm:NA");
    }
    uart_write("\r\n");
}

static void process_range_command(const char *command)
{
    RangeSelection range;
    const char *name;

    if ((command[1] < '0') || (command[1] > '4') ||
        (command[2] != '\0')) {
        uart_write("ERR: use R0, R1, R2, R3 or R4\r\n");
        return;
    }

    range = (RangeSelection) (command[1] - '0');
    autoRangeEnabled = 0U;
    autoRecheckDelay = 0U;
    RangeControl_select(range);

    name = range_name(range);

    if (range == RANGE_ALL_OFF) {
        uart_write("OK: auto range disabled; all range relays are off\r\n");
        return;
    }

    uart_write("OK: auto range disabled; selected ");
    uart_write(name);
    uart_write(" range\r\n");
}

static void process_command(char *command)
{
    uint32_t frequency = 0U;
    uint32_t index = 1U;

    if ((command[0] == '?') && (command[1] == '\0')) {
        uart_show_help();
        return;
    }

    if ((command[0] == 'R') || (command[0] == 'r')) {
        process_range_command(command);
        return;
    }

    if (((command[0] == 'A') || (command[0] == 'a')) &&
        ((command[1] == '0') || (command[1] == '1')) &&
        (command[2] == '\0')) {
        autoRangeEnabled = (uint8_t) (command[1] - '0');
        if (autoRangeEnabled != 0U) {
            uart_write("OK: auto range enabled (R1 and R3 only)\r\n");
            auto_select_range();
        } else {
            autoRecheckDelay = 0U;
            uart_write("OK: auto range disabled; current range retained\r\n");
        }
        return;
    }

    if (((command[0] == 'M') || (command[0] == 'm')) &&
        ((command[1] == '0') || (command[1] == '1')) &&
        (command[2] == '\0')) {
        monitorEnabled = (uint8_t) (command[1] - '0');
        uart_write((monitorEnabled != 0U) ?
            "OK: ADC monitor started\r\n" :
            "OK: ADC monitor stopped\r\n");
        return;
    }

    if ((command[0] != 'F') && (command[0] != 'f')) {
        uart_write("ERR: use F<number>, R0..R4, A0, A1, M0, M1 or ?\r\n");
        return;
    }

    if (command[1] == '\0') {
        uart_write("ERR: frequency is missing\r\n");
        return;
    }

    while (command[index] != '\0') {
        uint32_t digit;

        if ((command[index] < '0') || (command[index] > '9')) {
            uart_write("ERR: frequency must contain digits only\r\n");
            return;
        }

        digit = (uint32_t) (command[index] - '0');
        if (frequency > ((MAX_OUTPUT_HZ - digit) / 10U)) {
            uart_write("ERR: supported range is 1..100000 Hz\r\n");
            return;
        }

        frequency = (frequency * 10U) + digit;
        index++;
    }

    if ((frequency < MIN_OUTPUT_HZ) || (frequency > MAX_OUTPUT_HZ)) {
        uart_write("ERR: supported range is 1..100000 Hz\r\n");
        return;
    }

    AD9833_setSineFrequency(frequency);
    currentFrequencyHz = frequency;
    uart_write("OK: sine frequency = ");
    uart_write_uint32(frequency);
    uart_write(" Hz\r\n");

    if (autoRangeEnabled != 0U) {
        delay_cycles(CPUCLK_FREQ / 100U);
        auto_select_range();
    }
}

int main(void)
{
    char command[COMMAND_BUFFER_SIZE];
    uint32_t commandLength = 0U;
    uint32_t monitorElapsedMs = 0U;
    uint8_t received;

    SYSCFG_DL_init();

    /* Tianmengxing onboard LED: PB22, active high. */
    DL_GPIO_initDigitalOutput(BOARD_LED_IOMUX);
    DL_GPIO_setPins(GPIOB, BOARD_LED_PIN);
    DL_GPIO_enableOutput(GPIOB, BOARD_LED_PIN);

    AD9833_init();
    AD9833_setSineFrequency(AD9833_OUTPUT_HZ);
    RangeControl_init();
    ADCMonitor_init();

    delay_cycles(CPUCLK_FREQ / 20);
    uart_write("\r\nAD9833_READY: SINE 1000 Hz\r\n");
    uart_write("RANGE_READY: all relays off\r\n");
    uart_write(
        "ADC_MONITOR_READY: synchronous complex impedance reports every 500 ms\r\n");
    uart_show_help();

    while (1) {
        if (DL_UART_Main_receiveDataCheck(UART_0_INST, &received)) {
            if ((received == '\r') || (received == '\n')) {
                if (commandLength != 0U) {
                    command[commandLength] = '\0';
                    process_command(command);
                    commandLength = 0U;
                }
            } else if ((received == '\b') || (received == 0x7FU)) {
                if (commandLength != 0U) {
                    commandLength--;
                }
            } else if (commandLength < (COMMAND_BUFFER_SIZE - 1U)) {
                command[commandLength] = (char) received;
                commandLength++;
            } else {
                commandLength = 0U;
                uart_write("ERR: command is too long\r\n");
            }
        }

        delay_cycles(CPUCLK_FREQ / 1000U);
        monitorElapsedMs++;
        if ((monitorEnabled != 0U) &&
            (monitorElapsedMs >= MONITOR_INTERVAL_MS)) {
            monitorElapsedMs = 0U;
            uart_report_adc();
        }
    }
}
