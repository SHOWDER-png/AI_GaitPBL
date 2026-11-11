#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include "imu_init.h"

static int failures;

static void expect(const char *name, enum imu_status got, enum imu_status want)
{
	bool ok = (got == want);

	printf("TEST %-20s want=%-14s got=%-14s %s\n", name,
	       imu_status_str(want), imu_status_str(got), ok ? "PASS" : "FAIL");
	if (!ok) {
		failures++;
	}
}

/* USB serial output is lost until the PC opens the port, so wait (max 30 s). */
static void wait_for_host(void)
{
	const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	uint32_t dtr = 0;

	for (int i = 0; i < 300 && !dtr; i++) {
		uart_line_ctrl_get(uart, UART_LINE_CTRL_DTR, &dtr);
		k_msleep(100);
	}
	k_msleep(500);
}

int main(void)
{
	const struct device *const dev = DEVICE_DT_GET_ONE(bosch_bmi270);
	struct sensor_value acc[3], gyr[3];
	enum imu_status real;

	wait_for_host();
	printf("=== IMU init self-test (C29) ===\n");

	expect("simulated_not_ready", imu_init(dev, IMU_FAULT_NOT_READY), IMU_ERR_NOT_READY);
	expect("simulated_config", imu_init(dev, IMU_FAULT_CONFIG_INVALID), IMU_ERR_CONFIG_INVALID);
	expect("simulated_timeout", imu_init(dev, IMU_FAULT_TIMEOUT), IMU_ERR_TIMEOUT);

	real = imu_init(dev, IMU_FAULT_NONE);
	expect("real_hardware", real, IMU_OK);

	printf("=== self-test done: %d failure(s), imu_is_ready=%d ===\n",
	       failures, imu_is_ready());

	while (1) {
		if (!imu_is_ready()) {
			printf("IMU not ready (%s): sampling disabled\n", imu_status_str(real));
			k_msleep(2000);
			continue;
		}

		k_msleep(10);

		int rc = sensor_sample_fetch(dev);

		if (rc != 0) {
			printf("IMU: sample fetch failed, rc=%d\n", rc);
			continue;
		}
		rc = sensor_channel_get(dev, SENSOR_CHAN_ACCEL_XYZ, acc);
		if (rc == 0) {
			rc = sensor_channel_get(dev, SENSOR_CHAN_GYRO_XYZ, gyr);
		}
		if (rc != 0) {
			printf("IMU: channel get failed, rc=%d\n", rc);
			continue;
		}

		printf("AX: %d.%06d; AY: %d.%06d; AZ: %d.%06d; "
		       "GX: %d.%06d; GY: %d.%06d; GZ: %d.%06d;\n",
		       acc[0].val1, acc[0].val2, acc[1].val1, acc[1].val2,
		       acc[2].val1, acc[2].val2, gyr[0].val1, gyr[0].val2,
		       gyr[1].val1, gyr[1].val2, gyr[2].val1, gyr[2].val2);
	}

	return 0;
}