/*
 * Plantar thread. The only code that touches the ADS7128 (I2C) after init.
 *
 * Producer: every FSR_INTERVAL_MS (10 ms) reads 6 channels, prints to the USB
 * console and pushes a struct plantar_sample into the FSR queue (FIFO, 20 deep).
 * ble_thread is the consumer.
 */
#include <zephyr/kernel.h>
#include <stdio.h>

#include "plantar/fsr402.h"
#include "plantar/plantar_thread.h"
#include "system/system_config.h"

/* FIFO between this producer thread and ble_thread (the consumer). */
K_MSGQ_DEFINE(fsr_msgq, sizeof(struct plantar_sample), FSR_QUEUE_DEPTH, 4);

int plantar_queue_get(struct plantar_sample *s, k_timeout_t timeout)
{
	return k_msgq_get(&fsr_msgq, s, timeout);
}

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

		struct plantar_sample ps = { .timestamp_ms = t_ms };

		for (int i = 0; i < PLANTAR_CHANNELS; i++) {
			ps.fsr[i] = raw[i];
		}

		/* Queue full (consumer stalled/not connected): drop the OLDEST sample so
		 * the queue always holds the newest 200 ms.
		 */
		if (k_msgq_put(&fsr_msgq, &ps, K_NO_WAIT) != 0) {
			struct plantar_sample old;

			(void)k_msgq_get(&fsr_msgq, &old, K_NO_WAIT);
			(void)k_msgq_put(&fsr_msgq, &ps, K_NO_WAIT);
		}
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
