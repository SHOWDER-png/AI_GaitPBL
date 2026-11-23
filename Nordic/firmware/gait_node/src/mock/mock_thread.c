#include <zephyr/kernel.h>
#include <stdio.h>
#include <errno.h>

#include "ble/gait_ble.h"
#include "data/data_queue.h"
#include "mock/mock_signal.h"
#include "mock/mock_thread.h"
#include "system/system_config.h"

static K_THREAD_STACK_DEFINE(mock_stack, FSR_STACK_SIZE);
static struct k_thread mock_thread_data;

static void mock_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint32_t imu_sent = 0, imu_failed = 0, last_print = 0;
	int64_t next = k_uptime_get();

	while (1) {
		next += FSR_INTERVAL_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next));

		uint32_t t = k_uptime_get_32();
		struct plantar_sample ps;
		struct imu_sample is;

		/* Plantar -> queue -> ble_thread (same path as the real FSR thread) */
		mock_plantar(t, &ps);
		if (data_queue_put_plantar(&ps, K_NO_WAIT) != 0) {
			struct plantar_sample old;

			(void)data_queue_get_plantar(&old, K_NO_WAIT);
			(void)data_queue_put_plantar(&ps, K_NO_WAIT);
		}

		/* IMU -> BLE directly. mm/s^2 and mrad/s -> m/s^2 and rad/s (gait_ble converts back). */
		mock_imu(t, &is);

		struct gait_imu_sample gs = { .t_ms = t };

		for (int i = 0; i < 3; i++) {
			gs.acc[i] = is.accel[i] / 1000.0f;
			gs.gyr[i] = is.gyro[i] / 1000.0f;
		}

		int rc = gait_ble_send_imu(&gs);

		if (rc == 0) {
			imu_sent++;
		} else if (rc != -ENOTCONN && rc != -EPERM) {
			imu_failed++;
		}

		if (t - last_print >= 1000) {
			last_print = t;
			printf("MOCK: t=%u ms, IMU notifications sent %u, failed %u\n", t, imu_sent,
			       imu_failed);
		}
	}
}

int mock_thread_start(void)
{
	k_tid_t tid = k_thread_create(&mock_thread_data, mock_stack,
				      K_THREAD_STACK_SIZEOF(mock_stack), mock_thread_fn, NULL,
				      NULL, NULL, FSR_THREAD_PRIORITY, 0, K_NO_WAIT);

	if (tid == NULL) {
		return -ENOMEM;
	}

	k_thread_name_set(tid, "mock");
	return 0;
}
