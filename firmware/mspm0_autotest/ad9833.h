#ifndef AD9833_H
#define AD9833_H

#include <stdint.h>

/*
 * Tianmengxing MSPM0G3507 -> AD9833 wiring:
 *   PA12 -> SYNC / FSYNC
 *   PA13 -> CLK / SCLK
 *   PA14 -> DATA / SDATA
 *
 * Power the common 25 MHz AD9833 module from 3.3 V and connect grounds.
 */

#define AD9833_MCLK_HZ       (25000000UL)

void AD9833_init(void);
void AD9833_setSineFrequency(uint32_t frequencyHz);

#endif
