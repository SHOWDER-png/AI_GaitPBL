/*
 * gait_node main: initialise the modules and start one thread per sensor.
 * No sampling here: each sensor is read only by its own thread
 * (imu/imu_thread.c, plantar/plantar_thread.c).
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#ifdef CONFIG_GAIT_IMU
#include "imu/imu.h"
#include "imu/imu_thread.h"
#endif
#ifdef CONFIG_GAIT_PLANTAR
#include "plantar/fsr402.h"
#include "plantar/plantar_thread.h"
#endif
#ifdef CONFIG_GAIT_BLE
#include "ble/gait_ble.h"
#endif

int main(void)
{
	uint32_t dtr = 0;
	const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	/* Wait for USB serial host (up to 30 s) */
	for (int i = 0; i < 300 && !dtr; i++) {
		uart_line_ctrl_get(uart, UART_LINE_CTRL_DTR, &dtr);
		k_msleep(100);
	}
	k_msleep(500);

	printf("\n=== GAIT NODE STARTING ===\n");

#ifdef CONFIG_GAIT_IMU
	bool imu_ready = (imu_init() == 0);

	printf(imu_ready ? "BMI270 initialized\n" : "BMI270 initialization failed\n");
#endif

#ifdef CONFIG_GAIT_PLANTAR
	bool fsr_ready = (fsr402_init() == 0);

	printf(fsr_ready ? "FSR402 ADS7128 initialized\n"
			 : "FSR402 ADS7128 initialization failed\n");
#endif

#ifdef CONFIG_GAIT_BLE
	/* Start Bluetooth: advertise as "GaitNode" and wait for a central */
	int ble_rc = gait_ble_init();

	if (ble_rc != 0) {
		printf("BLE init failed: rc=%d\n", ble_rc);
	}
#endif

#ifdef CONFIG_GAIT_IMU
	if (imu_ready) {
		imu_thread_start();
	}
#endif
#ifdef CONFIG_GAIT_PLANTAR
	if (fsr_ready) {
		plantar_thread_start();
	}
#endif

	printf("\n=== GAIT NODE SAMPLING STARTED ===\n");

	/* The sensor threads keep running after main() returns. */
	return 0;
}
