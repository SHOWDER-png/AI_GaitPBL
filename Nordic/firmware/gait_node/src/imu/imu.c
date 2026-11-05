/*
 * IMU module (BMI270). Owner: IMU.
 * Settings are taken from experiments/imu_bmi270_c29 (±2 g, ±500 dps, 100 Hz).
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#include "imu/imu.h"

static const struct device *const imu_dev = DEVICE_DT_GET_ONE(bosch_bmi270);

static int set_attr(enum sensor_channel ch, enum sensor_attribute attr, int32_t val)
{
	struct sensor_value v = { .val1 = val, .val2 = 0 };

	return sensor_attr_set(imu_dev, ch, attr, &v);
}

int imu_init(void)
{
	int err = 0;

	if (!device_is_ready(imu_dev)) {
		return -ENODEV;
	}

	/* Sampling frequency is set last: it also selects the power mode (see C29 notes). */
	err |= set_attr(SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_FULL_SCALE, 2);      /* g */
	err |= set_attr(SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_OVERSAMPLING, 1);
	err |= set_attr(SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY,
			CONFIG_GAIT_IMU_RATE_HZ);

	err |= set_attr(SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_FULL_SCALE, 500);     /* dps */
	err |= set_attr(SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_OVERSAMPLING, 1);
	err |= set_attr(SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY,
			CONFIG_GAIT_IMU_RATE_HZ);

	return err ? -EIO : 0;
}

int imu_read(struct gait_imu_sample *out)
{
	struct sensor_value acc[3], gyr[3];
	int err;

	out->t_ms = k_uptime_get_32();

	err = sensor_sample_fetch(imu_dev);
	if (err) {
		return err;
	}
	sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_XYZ, acc);
	sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_XYZ, gyr);

	for (int i = 0; i < 3; i++) {
		out->acc[i] = (float)sensor_value_to_double(&acc[i]);
		out->gyr[i] = (float)sensor_value_to_double(&gyr[i]);
	}
	return 0;
}
