#include "range_control.h"

#include "ti_msp_dl_config.h"

/*
 * Tianmengxing MSPM0G3507 range-control outputs:
 *   PA8 -> 4.7 ohm relay
 *   PA9 -> 47 ohm relay
 *   PB4 -> 470 ohm relay
 *   PB5 -> 4.7 kohm relay
 *
 * The relay MOSFET drivers are active high.
 */
#define RANGE_4R7_PORT      GPIOA
#define RANGE_4R7_PIN       DL_GPIO_PIN_8
#define RANGE_4R7_IOMUX     IOMUX_PINCM19

#define RANGE_47R_PORT      GPIOA
#define RANGE_47R_PIN       DL_GPIO_PIN_9
#define RANGE_47R_IOMUX     IOMUX_PINCM20

#define RANGE_470R_PORT     GPIOB
#define RANGE_470R_PIN      DL_GPIO_PIN_4
#define RANGE_470R_IOMUX    IOMUX_PINCM17

#define RANGE_4K7_PORT      GPIOB
#define RANGE_4K7_PIN       DL_GPIO_PIN_5
#define RANGE_4K7_IOMUX     IOMUX_PINCM18

#define RELAY_BREAK_DELAY_CYCLES  (CPUCLK_FREQ / 100U)  /* 10 ms */
#define RELAY_SETTLE_DELAY_CYCLES (CPUCLK_FREQ / 50U)   /* 20 ms */

static RangeSelection activeRange = RANGE_ALL_OFF;

static void RangeControl_allOff(void)
{
    DL_GPIO_clearPins(
        GPIOA,
        RANGE_4R7_PIN | RANGE_47R_PIN);
    DL_GPIO_clearPins(
        GPIOB,
        RANGE_470R_PIN | RANGE_4K7_PIN);
    activeRange = RANGE_ALL_OFF;
}

void RangeControl_init(void)
{
    DL_GPIO_initDigitalOutput(RANGE_4R7_IOMUX);
    DL_GPIO_initDigitalOutput(RANGE_47R_IOMUX);
    DL_GPIO_initDigitalOutput(RANGE_470R_IOMUX);
    DL_GPIO_initDigitalOutput(RANGE_4K7_IOMUX);

    /* Set all control pins low before enabling their output drivers. */
    RangeControl_allOff();
    DL_GPIO_enableOutput(GPIOA, RANGE_4R7_PIN | RANGE_47R_PIN);
    DL_GPIO_enableOutput(GPIOB, RANGE_470R_PIN | RANGE_4K7_PIN);
}

void RangeControl_select(RangeSelection range)
{
    /*
     * Break-before-make: release every relay first. This guarantees that
     * software never deliberately energizes two range relays together.
     */
    RangeControl_allOff();
    delay_cycles(RELAY_BREAK_DELAY_CYCLES);

    switch (range) {
        case RANGE_4R7:
            DL_GPIO_setPins(RANGE_4R7_PORT, RANGE_4R7_PIN);
            break;
        case RANGE_47R:
            DL_GPIO_setPins(RANGE_47R_PORT, RANGE_47R_PIN);
            break;
        case RANGE_470R:
            DL_GPIO_setPins(RANGE_470R_PORT, RANGE_470R_PIN);
            break;
        case RANGE_4K7:
            DL_GPIO_setPins(RANGE_4K7_PORT, RANGE_4K7_PIN);
            break;
        case RANGE_ALL_OFF:
        default:
            return;
    }

    activeRange = range;
    delay_cycles(RELAY_SETTLE_DELAY_CYCLES);
}

RangeSelection RangeControl_getActive(void)
{
    return activeRange;
}
