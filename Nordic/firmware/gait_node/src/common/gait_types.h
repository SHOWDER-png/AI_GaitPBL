/*
 * Shared sample types for the gait node.
 * Owned by all three: change by PR and tag the other two.
 * Units follow the C29 IMU record (imu_bmi270_c29/imu_config.yaml).
 */
#ifndef GAIT_TYPES_H_
#define GAIT_TYPES_H_

#include <stdint.h>

/* One IMU reading. Axes are the BMI270 chip frame recorded in C29. */
struct gait_imu_sample {
	uint32_t t_ms;     /* k_uptime_get_32() when the sample was read */
	float acc[3];      /* m/s^2, x y z */
	float gyr[3];      /* rad/s, x y z */
};

/* FSR positions, in the order they appear in gait_plantar_sample.raw[] */
enum gait_fsr_pos {
	GAIT_FSR_L_HEEL = 0,
	GAIT_FSR_L_BALL,
	GAIT_FSR_L_TOE,
	GAIT_FSR_R_HEEL,
	GAIT_FSR_R_BALL,
	GAIT_FSR_R_TOE,
	GAIT_FSR_COUNT
};

/* One reading of all six plantar pressure sensors. */
struct gait_plantar_sample {
	uint32_t t_ms;                     /* k_uptime_get_32() when the read started */
	int16_t raw[GAIT_FSR_COUNT];       /* ADS1115 raw counts; calibration to force is done later */
};

#endif /* GAIT_TYPES_H_ */
