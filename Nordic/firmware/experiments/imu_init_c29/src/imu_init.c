#include <errno.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include "imu_init.h"

/* Attribute writes are idempotent, so repeating them is safe. */
#define ATTR_RETRY_MAX   3
#define ATTR_RETRY_MS    20
/* Reading a sample has no side effects, so repeating it is safe. */
#define SAMPLE_RETRY_MAX 10
#define SAMPLE_RETRY_MS  20

struct step {
	enum sensor_channel chan;
	enum sensor_attribute attr;
	int32_t val;
	const char *what;
};

/* Settings documented in imu_config.yaml (frequency is set last per sensor). */
static const struct step steps[] = {
	{SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_FULL_SCALE, 2, "accel full scale [g]"},
	{SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_OVERSAMPLING, 1, "accel oversampling"},
	{SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, 100, "accel rate [Hz]"},
	{SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_FULL_SCALE, 500, "gyro full scale [dps]"},
	{SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_OVERSAMPLING, 1, "gyro oversampling"},
	{SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, 100, "gyro rate [Hz]"},
};

static bool s_ready;

const char *imu_status_str(enum imu_status status)
{
	switch (status) {
	case IMU_OK:
		return "OK";
	case IMU_ERR_NOT_READY:
		return "NOT_READY";
	case IMU_ERR_CONFIG_INVALID:
		return "CONFIG_INVALID";
	case IMU_ERR_TIMEOUT:
		return "TIMEOUT";
	default:
		return "UNKNOWN";
	}
}

bool imu_is_ready(void)
{
	return s_ready;
}

/* With a fault set, these wrappers simulate the result and never touch the hardware. */
static bool dev_ready(const struct device *dev, enum imu_fault fault)
{
	if (fault == IMU_FAULT_NOT_READY) {
		return false;
	}
	return device_is_ready(dev);
}

static int attr_set(const struct device *dev, const struct step *s,
		    const struct sensor_value *val, enum imu_fault fault)
{
	if (fault == IMU_FAULT_CONFIG_INVALID) {
		return -EINVAL;
	}
	if (fault != IMU_FAULT_NONE) {
		return 0;
	}
	return sensor_attr_set(dev, s->chan, s->attr, val);
}

static int fetch(const struct device *dev, enum imu_fault fault)
{
	if (fault == IMU_FAULT_TIMEOUT) {
		return -EAGAIN;
	}
	if (fault != IMU_FAULT_NONE) {
		return 0;
	}
	return sensor_sample_fetch(dev);
}

static int attr_apply(const struct device *dev, const struct step *s, enum imu_fault fault)
{
	struct sensor_value val = {.val1 = s->val, .val2 = 0};
	int rc = 0;

	for (int i = 1; i <= ATTR_RETRY_MAX; i++) {
		rc = attr_set(dev, s, &val, fault);
		if (rc == 0) {
			return 0;
		}
		printf("IMU: set %s = %d failed, rc=%d (attempt %d/%d)\n",
		       s->what, (int)s->val, rc, i, ATTR_RETRY_MAX);
		if (rc == -EINVAL || rc == -ENOTSUP || rc == -ENOSYS) {
			break; /* the sensor rejected the value: retrying cannot help */
		}
		k_msleep(ATTR_RETRY_MS);
	}
	return rc;
}

/* A sample is valid if both channels read OK and gravity is visible on the accelerometer. */
static int verify_sample(const struct device *dev, enum imu_fault fault)
{
	struct sensor_value acc[3], gyr[3];

	for (int i = 1; i <= SAMPLE_RETRY_MAX; i++) {
		int rc = fetch(dev, fault);

		if (rc == 0) {
			int rca = sensor_channel_get(dev, SENSOR_CHAN_ACCEL_XYZ, acc);
			int rcg = sensor_channel_get(dev, SENSOR_CHAN_GYRO_XYZ, gyr);

			if (rca == 0 && rcg == 0) {
				double ax = sensor_value_to_double(&acc[0]);
				double ay = sensor_value_to_double(&acc[1]);
				double az = sensor_value_to_double(&acc[2]);

				if (ax * ax + ay * ay + az * az > 1.0) {
					return 0;
				}
				rc = -ENODATA; /* all zeros: sensor not delivering data yet */
			} else {
				rc = (rca != 0) ? rca : rcg;
			}
		}
		printf("IMU: sample check failed, rc=%d (attempt %d/%d)\n",
		       rc, i, SAMPLE_RETRY_MAX);
		k_msleep(SAMPLE_RETRY_MS);
	}
	return -ETIMEDOUT;
}

enum imu_status imu_init(const struct device *dev, enum imu_fault fault)
{
	enum imu_status status = IMU_OK;

	printf("IMU: init start%s\n", fault != IMU_FAULT_NONE ? " (SIMULATED FAULT)" : "");

	if (!dev_ready(dev, fault)) {
		printf("IMU: ERROR device not ready (missing, unpowered or driver init failed)\n");
		status = IMU_ERR_NOT_READY;
		goto done;
	}

	for (size_t i = 0; i < ARRAY_SIZE(steps); i++) {
		int rc = attr_apply(dev, &steps[i], fault);

		if (rc != 0) {
			if (rc == -EINVAL || rc == -ENOTSUP || rc == -ENOSYS) {
				printf("IMU: ERROR sensor rejected '%s' (rc=%d)\n",
				       steps[i].what, rc);
				status = IMU_ERR_CONFIG_INVALID;
			} else {
				printf("IMU: ERROR no response while setting '%s' (rc=%d)\n",
				       steps[i].what, rc);
				status = IMU_ERR_NOT_READY;
			}
			goto done;
		}
	}

	if (verify_sample(dev, fault) != 0) {
		printf("IMU: ERROR no valid sample after %d attempts\n", SAMPLE_RETRY_MAX);
		status = IMU_ERR_TIMEOUT;
		goto done;
	}

	printf("IMU: init OK (accel +/-2 g, gyro +/-500 dps, 100 Hz)\n");

done:
	/* Simulated faults never change the stored readiness. */
	if (fault == IMU_FAULT_NONE) {
		s_ready = (status == IMU_OK);
	}
	return status;
}