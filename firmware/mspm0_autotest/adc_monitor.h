#ifndef ADC_MONITOR_H
#define ADC_MONITOR_H

#include <stdint.h>

typedef struct {
    uint16_t excAverage;
    uint16_t excPeakToPeak;
    uint16_t senseAverage;
    uint16_t sensePeakToPeak;
    int32_t impedanceRealMilliOhm;
    int32_t impedanceImagMilliOhm;
    int32_t lossPpm;
    uint32_t capacitancePf;
    uint32_t samplesPerCycleX100;
    uint8_t impedanceValid;
} ADCMonitorResult;

void ADCMonitor_init(void);
void ADCMonitor_measure(ADCMonitorResult *result,
    uint32_t frequencyHz, uint32_t referenceMilliOhms);
uint32_t ADCMonitor_rawToMillivolts(uint16_t raw);

#endif
