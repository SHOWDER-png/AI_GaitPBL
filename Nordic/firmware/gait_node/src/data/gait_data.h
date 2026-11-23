/*
 * Sample types passed between threads through the queues (added in phase 4).
 * Raw values only: the node does no conversion except the units below.
 *
 * Not to be confused with common/gait_types.h, which is what gait_ble.c takes
 * today. ble_thread (phase 4) converts between the two.
 */
#ifndef GAIT_DATA_H_
#define GAIT_DATA_H_

#include <stdint.h>

#define PLANTAR_CHANNELS 6 /* L heel, L ball, L toe, R heel, R ball, R toe */

struct imu_sample {
	uint32_t timestamp_ms; /* k_uptime_get_32() when read */
	int16_t accel[3];      /* mm/s^2, x y z */
	int16_t gyro[3];       /* mrad/s, x y z */
};

struct plantar_sample {
	uint32_t timestamp_ms;          /* k_uptime_get_32() when the read started */
	uint16_t fsr[PLANTAR_CHANNELS]; /* raw ADC counts, 0..4095 (ADS7128, 12 bit) */
};

#endif /* GAIT_DATA_H_ */
