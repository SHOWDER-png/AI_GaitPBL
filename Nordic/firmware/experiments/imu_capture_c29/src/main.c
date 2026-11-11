/*
 * SCRUM-362: timestamped accelerometer + gyroscope capture on C29's Thingy:53
 *
 * Output: CSV text over USB CDC ACM (COM port), schema version 1.
 * Lines starting with '#' are metadata / statistics, not data records.
 *
 * Record: seq,t_us,ax_mps2,ay_mps2,az_mps2,gx_rads,gy_rads,gz_rads
 *   seq   = schedule slot number (0,1,2,...). A gap in seq = lost sample.
 *   t_us  = microseconds since boot, monotonic, taken just before the sensor read.
 */
#include <stdio.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include "imu_init.h"

#define SCHEMA_VERSION 1
#define SAMPLE_RATE_HZ 100
#define PERIOD_US      (1000000 / SAMPLE_RATE_HZ)
#define STAT_EVERY     100   /* print a '# STAT' line every N schedule slots */

static bool host_connected(void)
{
	const struct device *const dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	uint32_t dtr = 0;

	uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
	return dtr != 0;
}

/* USB serial output is lost until the PC opens the port: block until DTR is
 * asserted. Every new port open starts a fresh capture (header, seq=0). */
static void wait_for_host(void)
{
	while (!host_connected()) {
		k_msleep(50);
	}
	k_msleep(500);
}

static int64_t now_us(void)
{
	return k_ticks_to_us_floor64(k_uptime_ticks());
}

/* sensor_value -> "-1.234567" (no float printf needed) */
static void fmt_val(char *buf, size_t n, const struct sensor_value *v)
{
	int64_t m = (int64_t)v->val1 * 1000000 + v->val2;
	const char *sign = (m < 0) ? "-" : "";

	if (m < 0) {
		m = -m;
	}
	snprintf(buf, n, "%s%d.%06d", sign, (int)(m / 1000000), (int)(m % 1000000));
}

static void print_header(const char *init_result)
{
	int ticks = CONFIG_SYS_CLOCK_TICKS_PER_SEC;
	int res_x100 = (int)(100000000LL / ticks);

	printf("# schema_version=%d\n", SCHEMA_VERSION);
	printf("# unit=C29 board=thingy53/nrf5340/cpuapp sensor=BMI270\n");
	printf("# target_rate_hz=%d period_us=%d\n", SAMPLE_RATE_HZ, PERIOD_US);
	printf("# accel_range=+/-2g gyro_range=+/-500dps (set by imu_init)\n");
	printf("# t_us=microseconds since boot, monotonic, taken just before sensor fetch\n");
	printf("# clock_ticks_per_sec=%d resolution_us=%d.%02d\n",
	       ticks, res_x100 / 100, res_x100 % 100);
	printf("# units: accel=m/s^2 gyro=rad/s; axis order X,Y,Z (PCB arrows)\n");
	printf("# imu_init=%s\n", init_result);
	printf("seq,t_us,ax_mps2,ay_mps2,az_mps2,gx_rads,gy_rads,gz_rads\n");
}

int main(void)
{
	const struct device *const dev = DEVICE_DT_GET_ONE(bosch_bmi270);
	struct sensor_value acc[3], gyr[3];

	enum imu_status st = imu_init(dev, IMU_FAULT_NONE);

	while (1) {
		uint32_t seq = 0, written = 0, dropped = 0, read_errors = 0, last_stat = 0;

		wait_for_host();
		print_header(imu_status_str(st));
		if (st != IMU_OK || !imu_is_ready()) {
			printf("# ERROR: IMU not ready, sampling disabled\n");
			while (host_connected()) {
				k_msleep(100);
			}
			continue;
		}

		int64_t next = now_us();   /* slot 0 starts now */

		while (1) {
			/* wait for this slot's deadline (absolute schedule, no drift) */
			int64_t now = now_us();

			if (now < next) {
				k_usleep((int32_t)(next - now));
			}

			int64_t t_us = now_us();

			/* more than one period late: the missed slots are dropped */
			if (t_us >= next + PERIOD_US) {
				uint32_t missed = (uint32_t)((t_us - next) / PERIOD_US);

				dropped += missed;
				seq += missed;
				next += (int64_t)missed * PERIOD_US;
			}

			/* read both sensors; any failure rejects the whole sample */
			bool ok = (sensor_sample_fetch(dev) == 0) &&
				  (sensor_channel_get(dev, SENSOR_CHAN_ACCEL_XYZ, acc) == 0) &&
				  (sensor_channel_get(dev, SENSOR_CHAN_GYRO_XYZ, gyr) == 0);

			if (ok) {
				char s[6][16];

				for (int i = 0; i < 3; i++) {
					fmt_val(s[i], sizeof(s[i]), &acc[i]);
					fmt_val(s[3 + i], sizeof(s[3 + i]), &gyr[i]);
				}
				printf("%u,%llu,%s,%s,%s,%s,%s,%s\n", seq,
				       (unsigned long long)t_us,
				       s[0], s[1], s[2], s[3], s[4], s[5]);
				written++;
			} else {
				read_errors++;
			}

			seq++;
			next += PERIOD_US;

			if (seq - last_stat >= STAT_EVERY) {
				last_stat = seq;
				printf("# STAT slots=%u written=%u dropped=%u read_errors=%u\n",
				       seq, written, dropped, read_errors);
				if (!host_connected()) {
					break;   /* port closed: wait for the next capture */
				}
			}
		}
	}

	return 0;
}