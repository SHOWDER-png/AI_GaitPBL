
#include <zephyr/kernel.h>
#include <errno.h>

#include "data/data_queue.h"
#include "data/gait_data.h"

/* Separate queues for IMU and Plantar */
K_MSGQ_DEFINE(
    plantar_queue,
    sizeof(struct plantar_sample),
    DATA_QUEUE_DEPTH,
    4
);

K_MSGQ_DEFINE(
    imu_queue,
    sizeof(struct imu_sample),
    DATA_QUEUE_DEPTH,
    4
);

/* ---------- Plantar Queue ---------- */

int data_queue_put_plantar(
    const struct plantar_sample *sample,
    k_timeout_t timeout)
{
    if (sample == NULL) {
        return -EINVAL;
    }

    return k_msgq_put(
        &plantar_queue,
        sample,
        timeout
    );
}

int data_queue_get_plantar(
    struct plantar_sample *sample,
    k_timeout_t timeout)
{
    if (sample == NULL) {
        return -EINVAL;
    }

    return k_msgq_get(
        &plantar_queue,
        sample,
        timeout
    );
}

/* ---------- IMU Queue ---------- */

int data_queue_put_imu(
    const struct imu_sample *sample,
    k_timeout_t timeout)
{
    if (sample == NULL) {
        return -EINVAL;
    }

    return k_msgq_put(
        &imu_queue,
        sample,
        timeout
    );
}

int data_queue_get_imu(
    struct imu_sample *sample,
    k_timeout_t timeout)
{
    if (sample == NULL) {
        return -EINVAL;
    }

    return k_msgq_get(
        &imu_queue,
        sample,
        timeout
    );
}
