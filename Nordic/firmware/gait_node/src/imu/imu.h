/* IMU module (BMI270). Owner: IMU. */
#ifndef GAIT_IMU_H_
#define GAIT_IMU_H_

#include "common/gait_types.h"

/* Configure the BMI270 (range, rate). Returns 0 or a negative errno. */
int imu_init(void);

/* Fetch one accel + gyro sample into *out. Returns 0 or a negative errno. */
int imu_read(struct gait_imu_sample *out);

#endif /* GAIT_IMU_H_ */
