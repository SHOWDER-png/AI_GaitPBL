#ifndef FSR402_H
#define FSR402_H

#include <stdint.h>

#define FSR402_CHANNEL_COUNT 6

/*
 * Initialize the ADS7128 on the dedicated external I2C bus.
 * Returns 0 on success, or a negative Zephyr error code.
 */
int fsr402_init(void);

/*
 * Read the six FSR channels in this fixed order:
 *   raw[0] = Left Heel  (AIN0)
 *   raw[1] = Left Ball  (AIN1)
 *   raw[2] = Left Toe   (AIN2)
 *   raw[3] = Right Heel (AIN3)
 *   raw[4] = Right Ball (AIN4)
 *   raw[5] = Right Toe  (AIN5)
 *
 * Values are unsigned 12-bit ADC counts: 0..4095.
 */
int fsr402_read_all(uint16_t raw[FSR402_CHANNEL_COUNT]);

#endif /* FSR402_H */
