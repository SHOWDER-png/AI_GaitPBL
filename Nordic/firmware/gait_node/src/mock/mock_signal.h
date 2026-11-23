/*
 * Fake gait-like signals (stride 1.1 s) so BLE can be tested without any sensor.
 * Same units and types as real data: see data/gait_data.h.
 */
#ifndef MOCK_SIGNAL_H_
#define MOCK_SIGNAL_H_

#include "data/gait_data.h"

void mock_imu(uint32_t t_ms, struct imu_sample *s);      /* accel mm/s^2, gyro mrad/s */
void mock_plantar(uint32_t t_ms, struct plantar_sample *s); /* ADS7128 counts 0..4095 */

#endif /* MOCK_SIGNAL_H_ */
