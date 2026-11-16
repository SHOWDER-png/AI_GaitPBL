/*
 * IMU thread. The only code that touches the BMI270 after init.
 *
 * Phase 2: the thread prints each sample to the USB console, as main() used to.
 * Phase 4 replaces the printf with a push into the IMU queue.
 */
#include <zephyr/kernel.h>
#include <stdio.h>

#include "imu/imu.h"
#include "imu/imu_thread.h"
#include "system/system_config.h"

static K_THREAD_STACK_DEFINE(imu_stack, IMU_STACK_SIZE);
static struct k_thread imu_thread_data;

static void imu_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* Absolute deadlines: a fixed period without drift. */
	int64_t next = k_uptime_get();

	while (1) {
		struct gait_imu_sample s;
		int rc;

		next += IMU_INTERVAL_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next));

		/* Fell far behind (e.g. console blocked): skip ahead instead of bursting. */
		if (k_uptime_get() > next + IMU_INTERVAL_MS) {
			next = k_uptime_get();
		}

		rc = imu_read(&s);
		if (rc == 0) {
			printf("IMU,t_ms=%u,AX=%.6f,AY=%.6f,AZ=%.6f,GX=%.6f,GY=%.6f,GZ=%.6f\n",
			       s.t_ms, (double)s.acc[0], (double)s.acc[1], (double)s.acc[2],
			       (double)s.gyr[0], (double)s.gyr[1], (double)s.gyr[2]);
		} else {
			printf("IMU read failed: rc=%d\n", rc);
		}
	}
}

int imu_thread_start(void)
{
	k_tid_t tid = k_thread_create(&imu_thread_data, imu_stack,
				      K_THREAD_STACK_SIZEOF(imu_stack), imu_thread_fn,
				      NULL, NULL, NULL, IMU_THREAD_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(tid, "imu");
	return 0;
}
