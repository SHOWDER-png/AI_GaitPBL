
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>
#include <errno.h>

#include "data/data_queue.h"
#include "data/gait_data.h"
#include "ble/gait_ble.h"
#include "ble/ble_thread.h"
#include "system/system_config.h"


static K_THREAD_STACK_DEFINE(
    ble_stack,
    BLE_STACK_SIZE
);

static struct k_thread ble_thread_data;

/* Consume FSR data from the shared queue */
static void consume_plantar_data(
    void *p1,
    void *p2,
    void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    struct plantar_sample sample;

    while (1) {

        /* Wait for the next FSR sample */
        int rc = data_queue_get_plantar(
            &sample,
            K_FOREVER
        );

        if (rc != 0) {
            continue;
        }

        /* Convert to existing BLE data structure */
        struct gait_plantar_sample ps = {
            .t_ms = sample.timestamp_ms
        };

        for (int i = 0; i < PLANTAR_CHANNELS; i++) {
            ps.raw[i] = (int16_t)sample.fsr[i];
        }

        /* Send FSR notification */
        rc = gait_ble_send_plantar(&ps);

        if (rc != 0 && rc != -ENOTCONN) {
            printk("BLE: plantar send failed: %d\n", rc);
        }
    }
}

/* Start BLE consumer thread */
int ble_thread_start(void)
{
    k_tid_t tid = k_thread_create(
        &ble_thread_data,
        ble_stack,
        K_THREAD_STACK_SIZEOF(ble_stack),
        consume_plantar_data,
        NULL,
        NULL,
        NULL,
        BLE_THREAD_PRIORITY,
        0,
        K_NO_WAIT
    );

    if (tid == NULL) {
        return -ENOMEM;
    }

    k_thread_name_set(tid, "ble_consumer");

    return 0;
}
