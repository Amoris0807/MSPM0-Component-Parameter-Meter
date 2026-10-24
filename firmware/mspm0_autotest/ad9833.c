#include "ad9833.h"

#include <ti/devices/msp/msp.h>
#include <ti/driverlib/driverlib.h>
#include <ti/driverlib/m0p/dl_core.h>

#define AD9833_PORT          GPIOA

#define AD9833_SYNC_PIN      DL_GPIO_PIN_12
#define AD9833_CLK_PIN       DL_GPIO_PIN_13
#define AD9833_DATA_PIN      DL_GPIO_PIN_14

#define AD9833_SYNC_IOMUX    IOMUX_PINCM34
#define AD9833_CLK_IOMUX     IOMUX_PINCM35
#define AD9833_DATA_IOMUX    IOMUX_PINCM36

#define AD9833_CTRL_B28      (0x2000U)
#define AD9833_CTRL_RESET    (0x0100U)
#define AD9833_FREQ0_ADDR    (0x4000U)
#define AD9833_PHASE0_ADDR   (0xC000U)

static void AD9833_shortDelay(void)
{
    /* At 32 MHz this is comfortably longer than the AD9833 timing minimum. */
    delay_cycles(8);
}

static void AD9833_writeWord(uint16_t word)
{
    uint16_t mask;

    DL_GPIO_clearPins(AD9833_PORT, AD9833_SYNC_PIN);
    AD9833_shortDelay();

    for (mask = 0x8000U; mask != 0U; mask >>= 1U) {
        if ((word & mask) != 0U) {
            DL_GPIO_setPins(AD9833_PORT, AD9833_DATA_PIN);
        } else {
            DL_GPIO_clearPins(AD9833_PORT, AD9833_DATA_PIN);
        }

        AD9833_shortDelay();
        /* AD9833 samples SDATA on SCLK's falling edge. */
        DL_GPIO_clearPins(AD9833_PORT, AD9833_CLK_PIN);
        AD9833_shortDelay();
        DL_GPIO_setPins(AD9833_PORT, AD9833_CLK_PIN);
    }

    AD9833_shortDelay();
    DL_GPIO_setPins(AD9833_PORT, AD9833_SYNC_PIN);
}

void AD9833_init(void)
{
    DL_GPIO_initDigitalOutput(AD9833_SYNC_IOMUX);
    DL_GPIO_initDigitalOutput(AD9833_CLK_IOMUX);
    DL_GPIO_initDigitalOutput(AD9833_DATA_IOMUX);

    /* Idle state: FSYNC high, SCLK high, SDATA low. */
    DL_GPIO_setPins(AD9833_PORT, AD9833_SYNC_PIN | AD9833_CLK_PIN);
    DL_GPIO_clearPins(AD9833_PORT, AD9833_DATA_PIN);
    DL_GPIO_enableOutput(
        AD9833_PORT,
        AD9833_SYNC_PIN | AD9833_CLK_PIN | AD9833_DATA_PIN);

    AD9833_writeWord(AD9833_CTRL_B28 | AD9833_CTRL_RESET);
    AD9833_writeWord(AD9833_FREQ0_ADDR);
    AD9833_writeWord(AD9833_FREQ0_ADDR);
    AD9833_writeWord(AD9833_PHASE0_ADDR);
    AD9833_writeWord(AD9833_CTRL_B28);
}

void AD9833_setSineFrequency(uint32_t frequencyHz)
{
    uint32_t tuningWord;
    uint16_t low14;
    uint16_t high14;

    /*
     * tuningWord = frequencyHz * 2^28 / MCLK.
     * The added half divisor rounds to the nearest tuning word.
     */
    tuningWord = (uint32_t) (
        ((((uint64_t) frequencyHz) << 28U) + (AD9833_MCLK_HZ / 2U)) /
        AD9833_MCLK_HZ);

    if (tuningWord > 0x0FFFFFFFUL) {
        tuningWord = 0x0FFFFFFFUL;
    }

    low14  = (uint16_t) (tuningWord & 0x3FFFU);
    high14 = (uint16_t) ((tuningWord >> 14U) & 0x3FFFU);

    AD9833_writeWord(AD9833_CTRL_B28 | AD9833_CTRL_RESET);
    AD9833_writeWord((uint16_t) (AD9833_FREQ0_ADDR | low14));
    AD9833_writeWord((uint16_t) (AD9833_FREQ0_ADDR | high14));
    AD9833_writeWord(AD9833_PHASE0_ADDR);
    AD9833_writeWord(AD9833_CTRL_B28);
}
