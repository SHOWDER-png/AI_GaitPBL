
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>

#include "plantar/fsr402.h"

int main(void)
{
    /* BMI270 IMU - Kaan's original sensor approach */
    const struct device *const dev =
        DEVICE_DT_GET_ONE(bosch_bmi270);

    struct sensor_value acc[3], gyr[3];
    struct sensor_value full_scale;
    struct sensor_value sampling_freq;
    struct sensor_value oversampling;

    /* ADS7128 / FSR402 - six channels */
    uint16_t fsr_raw[FSR402_CHANNEL_COUNT];
    bool fsr_ready = false;

    uint32_t dtr = 0;
    const struct device *uart =
        DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

    /* Wait for USB serial host */
    for (int i = 0; i < 300 && !dtr; i++) {
        uart_line_ctrl_get(uart, UART_LINE_CTRL_DTR, &dtr);
        k_msleep(100);
    }
    k_msleep(500);

    printf("\n=== GAIT NODE STARTING ===\n");

    /* Initialize BMI270 */
    if (!device_is_ready(dev)) {
        printf("BMI270 device is not ready\n");
    } else {
        printf("BMI270 device ready: %s\n", dev->name);

        /* Accelerometer settings */
        full_scale.val1 = 2;
        full_scale.val2 = 0;

        sampling_freq.val1 = 100;
        sampling_freq.val2 = 0;

        oversampling.val1 = 1;
        oversampling.val2 = 0;

        sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ,
                        SENSOR_ATTR_FULL_SCALE, &full_scale);

        sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ,
                        SENSOR_ATTR_OVERSAMPLING, &oversampling);

        sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ,
                        SENSOR_ATTR_SAMPLING_FREQUENCY,
                        &sampling_freq);

        /* Gyroscope settings */
        full_scale.val1 = 500;
        full_scale.val2 = 0;

        sensor_attr_set(dev, SENSOR_CHAN_GYRO_XYZ,
                        SENSOR_ATTR_FULL_SCALE, &full_scale);

        sensor_attr_set(dev, SENSOR_CHAN_GYRO_XYZ,
                        SENSOR_ATTR_OVERSAMPLING, &oversampling);

        sensor_attr_set(dev, SENSOR_CHAN_GYRO_XYZ,
                        SENSOR_ATTR_SAMPLING_FREQUENCY,
                        &sampling_freq);
    }

    /* Initialize ADS7128 / FSR402 */
    printf("\n=== FSR402 ADS7128 initialization ===\n");

    if (fsr402_init() == 0) {
        fsr_ready = true;
        printf("FSR402 ADS7128 initialized\n");
    } else {
        printf("FSR402 ADS7128 initialization failed\n");
    }

    printf("\n=== GAIT NODE SAMPLING STARTED ===\n");

    while (1) {
        k_msleep(10);

        /* Read BMI270 */
        if (device_is_ready(dev)) {
            int rc = sensor_sample_fetch(dev);

            if (rc == 0) {
                rc = sensor_channel_get(
                    dev, SENSOR_CHAN_ACCEL_XYZ, acc);

                if (rc == 0) {
                    rc = sensor_channel_get(
                        dev, SENSOR_CHAN_GYRO_XYZ, gyr);
                }
            }

            if (rc == 0) {
                printf(
                    "IMU,AX=%d.%06d,AY=%d.%06d,AZ=%d.%06d,"
                    "GX=%d.%06d,GY=%d.%06d,GZ=%d.%06d\n",
                    acc[0].val1, acc[0].val2,
                    acc[1].val1, acc[1].val2,
                    acc[2].val1, acc[2].val2,
                    gyr[0].val1, gyr[0].val2,
                    gyr[1].val1, gyr[1].val2,
                    gyr[2].val1, gyr[2].val2
                );
            } else {
                printf("IMU read failed: rc=%d\n", rc);
            }
        }

        /* Read six FSR402 channels through ADS7128 */
        if (fsr_ready) {
            int rc = fsr402_read_all(fsr_raw);

            if (rc == 0) {
                printf(
                    "FSR,t_ms=%u,LH=%u,LB=%u,LT=%u,"
                    "RH=%u,RB=%u,RT=%u\n",
                    k_uptime_get_32(),
                    fsr_raw[0], fsr_raw[1], fsr_raw[2],
                    fsr_raw[3], fsr_raw[4], fsr_raw[5]
                );
            } else {
                printf("FSR read failed: rc=%d\n", rc);
            }
        }
    }

    return 0;
}
