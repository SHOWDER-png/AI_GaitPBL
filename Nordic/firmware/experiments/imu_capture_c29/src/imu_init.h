#ifndef IMU_INIT_H_
#define IMU_INIT_H_

#include <stdbool.h>
#include <zephyr/device.h>

/* Result of IMU initialization. Only IMU_OK means "ready to sample". */
enum imu_status {
	IMU_OK = 0,
	IMU_ERR_NOT_READY,      /* device missing or driver init failed */
	IMU_ERR_CONFIG_INVALID, /* sensor rejected a setting */
	IMU_ERR_TIMEOUT,        /* no valid sample within the retry limit */
};

/* Fault injection, used only for tests. IMU_FAULT_NONE = real hardware. */
enum imu_fault {
	IMU_FAULT_NONE = 0,
	IMU_FAULT_NOT_READY,
	IMU_FAULT_CONFIG_INVALID,
	IMU_FAULT_TIMEOUT,
};

/* Applies the documented settings (accel +/-2 g, gyro +/-500 dps, 100 Hz,
 * oversampling 1), checks every return value and verifies one real sample.
 * The result is remembered for imu_is_ready().
 */
enum imu_status imu_init(const struct device *dev, enum imu_fault fault);

/* True only if the last imu_init() returned IMU_OK. */
bool imu_is_ready(void);

const char *imu_status_str(enum imu_status status);

#endif /* IMU_INIT_H_ */