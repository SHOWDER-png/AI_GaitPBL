/*
 * BLE consumer thread. Takes samples from the FSR queue as soon as they exist
 * (never waits for the queue to fill) and sends each as one 18-byte notification
 * through gait_ble_send_plantar().
 */
#include <zephyr/kernel.h>
#include <errno.h>
#include <stdio.h>

#include "ble/ble_thread.h"
#include "ble/gait_ble.h"
#include "plantar/plantar_thread.h"
#include "system/system_config.h"

#define SEND_RETRIES 5

static K_THREAD_STACK_DEFINE(ble_stack, BLE_STACK_SIZE);
static struct k_thread ble_thread_data;

static void ble_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint32_t dropped = 0;

	while (1) {
		struct plantar_sample ps;
		struct gait_plantar_sample out;
		int rc;

		/* Sleep until the FSR thread pushes a sample */
		if (plantar_queue_get(&ps, K_FOREVER) != 0) {
			continue;
		}

		out.t_ms = ps.timestamp_ms;
		for (int i = 0; i < GAIT_FSR_COUNT; i++) {
			out.raw[i] = (int16_t)ps.fsr[i]; /* 0..4095 fits */
		}

		/* -ENOMEM/-EAGAIN: controller buffers full (connection interval is
		 * the real limit). Wait 1 ms and retry a few times, then drop.
		 */
		for (int n = 0; n < SEND_RETRIES; n++) {
			rc = gait_ble_send_plantar(&out);
			if (rc != -ENOMEM && rc != -EAGAIN) {
				break;
			}
			k_msleep(1);
		}

		/* -ENOTCONN (not subscribed) is normal: sample is discarded so a
		 * new central never receives stale data.
		 */
		if (rc != 0 && rc != -ENOTCONN) {
			dropped++;
			if ((dropped % 100) == 1) {
				printf("BLE: plantar send failed rc=%d (dropped=%u)\n", rc, dropped);
			}
		}
	}
}

int ble_thread_start(void)
{
	k_tid_t tid = k_thread_create(&ble_thread_data, ble_stack,
				      K_THREAD_STACK_SIZEOF(ble_stack), ble_thread_fn,
				      NULL, NULL, NULL, BLE_THREAD_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(tid, "ble");
	return 0;
}
