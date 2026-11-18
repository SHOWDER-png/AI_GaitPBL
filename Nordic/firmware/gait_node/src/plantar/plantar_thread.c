
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>
#include <errno.h>

#include "plantar/fsr402.h"
#include "plantar/plantar_thread.h"
#include "system/system_config.h"

/* Sampling thread */
static K_THREAD_STACK_DEFINE(
    fsr_stack, FSR_STACK_SIZE);
static struct k_thread fsr_thread_data;

/* Shared queue between sampling and BLE threads */
K_MSGQ_DEFINE(
    fsr_sample_queue,
    sizeof(plantar_sample_t),
    PLANTAR_QUEUE_DEPTH,
    4
);

/* Sampling thread */
static void plantar_thread_fn(
    void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    int64_t next = k_uptime_get();
    while (1) {
        uint16_t raw[FSR402_CHANNEL_COUNT];
        plantar_sample_t sample;
        next += FSR_INTERVAL_MS;
        /* Maintain fixed sampling interval */
        k_sleep(K_TIMEOUT_ABS_MS(next));
        /* Skip missed periods instead of burst reading */
        if (k_uptime_get() > next + FSR_INTERVAL_MS) {
            next = k_uptime_get();
        }

        /* Record timestamp before ADC reading */
        sample.t_ms = k_uptime_get_32();
        /* Read all six FSR sensors */
        int rc = fsr402_read_all(raw);
        if (rc != 0) {
            printk("FSR read error: %d\n", rc);
            continue;
        }
        /* Copy sensor data */
        for (int i = 0; i < FSR402_CHANNEL_COUNT; i++) {
            sample.raw[i] = raw[i];
        }
        /* Push sample into queue without blocking */
        rc = k_msgq_put(
            &fsr_sample_queue,
            &sample,
            K_NO_WAIT
        );
        if (rc != 0) {
            printk("FSR queue full: sample dropped\n");
        }
    }
}

/* Start sampling thread */
int plantar_thread_start(void)
{
    k_tid_t tid = k_thread_create(
        &fsr_thread_data,
        fsr_stack,
        K_THREAD_STACK_SIZEOF(fsr_stack),
        plantar_thread_fn,
        NULL, NULL, NULL,
        FSR_THREAD_PRIORITY,
        0,
        K_NO_WAIT
    );

    if (tid == NULL) {
        return -ENOMEM;
    }
    k_thread_name_set(tid, "plantar");
    return 0;
}

/* Allow another thread to retrieve FSR samples */
int plantar_get_sample(
    plantar_sample_t *sample,
    k_timeout_t timeout)
{
    if (sample == NULL) {
        return -EINVAL;
    }
    return k_msgq_get(
        &fsr_sample_queue,
        sample,
        timeout
    );
}
