
#ifndef DATA_QUEUE_H_
#define DATA_QUEUE_H_

#include <zephyr/kernel.h>
#include "data/gait_data.h"

#define DATA_QUEUE_DEPTH 20

/* Plantar queue */
int data_queue_put_plantar(
    const struct plantar_sample *sample,
    k_timeout_t timeout
);

int data_queue_get_plantar(
    struct plantar_sample *sample,
    k_timeout_t timeout
);

/* IMU queue */
int data_queue_put_imu(
    const struct imu_sample *sample,
    k_timeout_t timeout
);

int data_queue_get_imu(
    struct imu_sample *sample,
    k_timeout_t timeout
);

#endif /* DATA_QUEUE_H_ */
