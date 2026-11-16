/*
 * Plantar thread. The only code that touches the ADS7128 (I2C) after init.
 *
 * Phase 3: the thread prints each reading to the USB console and, when BLE is
 * enabled, sends it straight to gait_ble, exactly as main() used to.
 * Phase 4 replaces both with a push into the FSR queue; ble_thread then sends.
 */
#include <zephyr/kernel.h>
#include <stdio.h>

#include "plantar/fsr402.h"
#include "plantar/plantar_thread.h"
#include "system/system_config.h"

#ifdef CONFIG_GAIT_BLE
#include "ble/gait_ble.h"
#endif

static K_THREAD_STACK_DEFINE(fsr_stack, FSR_STACK_SIZE);
static struct k_thread fsr_thread_data;

static void plantar_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int64_t next = k_uptime_get();

	while (1) {
		uint16_t raw[FSR402_CHANNEL_COUNT];
		uint32_t t_ms;
		int rc;

		next += FSR_INTERVAL_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next));

		/* Fell far behind: skip ahead instead of bursting. */
		if (k_uptime_get() > next + FSR_INTERVAL_MS) {
			next = k_uptime_get();
		}

		t_ms = k_uptime_get_32();
		rc = fsr402_read_all(raw);
		if (rc != 0) {
			printf("FSR read failed: rc=%d\n", rc);
			continue;
		}

		printf("FSR,t_ms=%u,LH=%u,LB=%u,LT=%u,RH=%u,RB=%u,RT=%u\n", t_ms,
		       raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]);

#ifdef CONFIG_GAIT_BLE
		/* Temporary (until phase 4). Returns -ENOTCONN, ignored, while no
		 * central has subscribed.
		 */
		struct gait_plantar_sample ps = { .t_ms = t_ms };

		for (int i = 0; i < GAIT_FSR_COUNT; i++) {
			ps.raw[i] = (int16_t)raw[i];
		}
		(void)gait_ble_send_plantar(&ps);
#endif
	}
}

int plantar_thread_start(void)
{
	k_tid_t tid = k_thread_create(&fsr_thread_data, fsr_stack,
				      K_THREAD_STACK_SIZEOF(fsr_stack), plantar_thread_fn,
				      NULL, NULL, NULL, FSR_THREAD_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(tid, "plantar");
	return 0;
}
