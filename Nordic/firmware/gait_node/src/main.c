
/*
 * gait_node main:
 * Initialise modules and start sensor threads.
 * Each sensor is read only by its own sampling thread.
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

#if defined(CONFIG_GAIT_BLE) && defined(CONFIG_GAIT_PLANTAR)
#include "ble/ble_thread.h"
#endif

int main(void)
{
    uint32_t dtr = 0;
    const struct device *uart =
        DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

    /* Wait for USB serial host (up to 30 seconds) */
    for (int i = 0; i < 300 && !dtr; i++) {
        uart_line_ctrl_get(
            uart,
            UART_LINE_CTRL_DTR,
            &dtr
        );
        k_msleep(100);
    }

    k_msleep(500);

    printf("\n=== GAIT NODE STARTING ===\n");

#ifdef CONFIG_GAIT_IMU
    bool imu_ready = (imu_init() == 0);

    printf(imu_ready ?
           "BMI270 initialized\n" :
           "BMI270 initialization failed\n");
#endif

#ifdef CONFIG_GAIT_PLANTAR
    bool fsr_ready = (fsr402_init() == 0);

    printf(fsr_ready ?
           "FSR402 ADS7128 initialized\n" :
           "FSR402 ADS7128 initialization failed\n");
#endif

#ifdef CONFIG_GAIT_BLE
    /* Initialize Bluetooth */
    int ble_rc = gait_ble_init();

    bool ble_ready = (ble_rc == 0);

    if (!ble_ready) {
        printf("BLE init failed: rc=%d\n", ble_rc);
    }
#endif

#ifdef CONFIG_GAIT_IMU
    if (imu_ready) {
        int rc = imu_thread_start();

        if (rc != 0) {
            printf("IMU thread start failed: %d\n", rc);
        } else {
            printf("IMU sampling thread started\n");
        }
    }
#endif

#ifdef CONFIG_GAIT_PLANTAR
    if (fsr_ready) {
        int rc = plantar_thread_start();

        if (rc != 0) {
            printf("FSR thread start failed: %d\n", rc);
        } else {
            printf("FSR sampling thread started\n");
        }
    }
#endif

#if defined(CONFIG_GAIT_BLE) && defined(CONFIG_GAIT_PLANTAR)
    if (ble_ready && fsr_ready) {
        int rc = ble_thread_start();

        if (rc != 0) {
            printf("BLE consumer thread start failed: %d\n", rc);
        } else {
            printf("BLE consumer thread started\n");
        }
    }
#endif

    printf("\n=== GAIT NODE SAMPLING STARTED ===\n");

    return 0;
}
