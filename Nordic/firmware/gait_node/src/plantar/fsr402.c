
/*
 * FSR402 + Adafruit ADS7128
 * Nordic Thingy:53 - Zephyr / nRF Connect SDK
 *
 * ADS7128 I2C address: 0x10
 * Channels:
 * A0 = Left Heel
 * A1 = Left Ball
 * A2 = Left Toe
 * A3 = Right Heel
 * A4 = Right Ball
 * A5 = Right Toe
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <errno.h>
#include <stdint.h>

#include "plantar/fsr402.h"

/* ---------- ADS7128 configuration ---------- */

#define ADS7128_I2C_ADDR       0x10

/* I2C command opcodes */
#define ADS7128_CMD_REG_READ   0x10
#define ADS7128_CMD_REG_WRITE  0x08

/* ADS7128 register addresses */
#define ADS7128_REG_SYSTEM_STATUS  0x00
#define ADS7128_REG_DATA_CFG       0x02
#define ADS7128_REG_OSR_CFG        0x03
#define ADS7128_REG_OPMODE_CFG     0x04
#define ADS7128_REG_PIN_CFG        0x05
#define ADS7128_REG_SEQUENCE_CFG   0x10
#define ADS7128_REG_CHANNEL_SEL    0x11

/*
 * This implementation uses I2C controller i2c0.
 * Confirm that i2c0 is routed to the external ADS7128
 * SDA/SCL pins in app.overlay.
 */
static const struct device *const i2c_dev =
    DEVICE_DT_GET(DT_NODELABEL(i2c0));

/* ---------- Low-level I2C functions ---------- */

/*
 * Write one ADS7128 register.
 * Format: OPCODE, REGISTER ADDRESS, DATA
 */
static int ads7128_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx[3] = {
        ADS7128_CMD_REG_WRITE,
        reg,
        value
    };

    return i2c_write(i2c_dev, tx, sizeof(tx),
                     ADS7128_I2C_ADDR);
}

/*
 * Read one ADS7128 register.
 * First send read opcode and register address,
 * then perform a separate I2C read.
 */
static int ads7128_read_reg(uint8_t reg, uint8_t *value)
{
    uint8_t tx[2] = {
        ADS7128_CMD_REG_READ,
        reg
    };

    int ret = i2c_write(i2c_dev, tx, sizeof(tx),
                        ADS7128_I2C_ADDR);

    if (ret != 0) {
        return ret;
    }

    return i2c_read(i2c_dev, value, 1,
                    ADS7128_I2C_ADDR);
}

/* ---------- Public FSR functions ---------- */

/*
 * Initialize I2C and ADS7128.
 */
int fsr402_init(void)
{
    uint8_t status;
    int ret;

    if (!device_is_ready(i2c_dev)) {
        printk("FSR: I2C0 device is not ready\n");
        return -ENODEV;
    }

    /* Wait for ADS7128 power-up */
    k_msleep(10);

    /* Check ADS7128 communication */
    ret = ads7128_read_reg(
        ADS7128_REG_SYSTEM_STATUS, &status);

    if (ret != 0) {
        printk("FSR: ADS7128 I2C communication failed: %d\n",
               ret);
        return ret;
    }

    printk("FSR: ADS7128 detected, status=0x%02X\n",
           status);

    /*
     * Configure normal ADC output:
     * - No fixed test pattern
     * - No appended channel/status bits
     */
    ret = ads7128_write_reg(ADS7128_REG_DATA_CFG, 0x00);

    if (ret != 0) {
        return ret;
    }

    /* Disable averaging: direct 12-bit ADC result */
    ret = ads7128_write_reg(ADS7128_REG_OSR_CFG, 0x00);

    if (ret != 0) {
        return ret;
    }

    /* Manual conversion mode */
    ret = ads7128_write_reg(ADS7128_REG_OPMODE_CFG, 0x00);

    if (ret != 0) {
        return ret;
    }

    /* Configure all channels as analog inputs */
    ret = ads7128_write_reg(ADS7128_REG_PIN_CFG, 0x00);

    if (ret != 0) {
        return ret;
    }

    /* Manual sequence mode */
    ret = ads7128_write_reg(
        ADS7128_REG_SEQUENCE_CFG, 0x00);

    if (ret != 0) {
        return ret;
    }

    printk("FSR: ADS7128 initialization complete\n");

    return 0;
}

/*
 * Read one ADC channel.
 *
 * channel = 0 to 7
 * raw_value = 12-bit ADC result (0 to 4095)
 */
static int ads7128_read_channel(uint8_t channel,
                                uint16_t *raw_value)
{
    uint8_t data[2];
    int ret;

    if (channel > 7 || raw_value == NULL) {
        return -EINVAL;
    }

    /*
     * Select the next analog input channel.
     * CHANNEL_SEL[3:0] = MANUAL_CHID
     */
    ret = ads7128_write_reg(
        ADS7128_REG_CHANNEL_SEL, channel);

    if (ret != 0) {
        return ret;
    }

    /*
     * Read conversion data.
     * The ADS7128 starts conversion during the read frame.
     * It may stretch SCL while conversion is in progress.
     */
    ret = i2c_read(i2c_dev, data, sizeof(data),
                   ADS7128_I2C_ADDR);

    if (ret != 0) {
        return ret;
    }

    /*
     * 12-bit data format:
     * Byte 0 = D11..D4
     * Byte 1 upper nibble = D3..D0
     */
    *raw_value =
        ((uint16_t)data[0] << 4) |
        ((uint16_t)data[1] >> 4);

    return 0;
}

/*
 * Read all six FSR402 sensors.
 *
 * raw[0] = Left Heel  (A0)
 * raw[1] = Left Ball  (A1)
 * raw[2] = Left Toe   (A2)
 * raw[3] = Right Heel (A3)
 * raw[4] = Right Ball (A4)
 * raw[5] = Right Toe  (A5)
 */
int fsr402_read_all(uint16_t raw[FSR402_CHANNEL_COUNT])
{
    int ret;

    if (raw == NULL) {
        return -EINVAL;
    }

    for (uint8_t channel = 0;
         channel < FSR402_CHANNEL_COUNT;
         channel++) {

        ret = ads7128_read_channel(channel, &raw[channel]);

        if (ret != 0) {
            return ret;
        }
    }

    return 0;
}
