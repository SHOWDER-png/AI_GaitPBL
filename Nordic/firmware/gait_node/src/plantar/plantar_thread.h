
#ifndef GAIT_PLANTAR_THREAD_H_
#define GAIT_PLANTAR_THREAD_H_

#include <stdint.h>
#include <zephyr/kernel.h>

#define PLANTAR_QUEUE_DEPTH 20

typedef struct {
    uint32_t t_ms;
    uint16_t raw[6];
} plantar_sample_t;

/* Start periodic FSR sampling thread */
int plantar_thread_start(void);

/* Get one sample from the FSR queue */
int plantar_get_sample(plantar_sample_t *sample,
                       k_timeout_t timeout);

#endif
