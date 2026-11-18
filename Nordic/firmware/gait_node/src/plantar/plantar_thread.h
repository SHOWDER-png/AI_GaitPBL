/* Plantar thread: decides WHEN to read the six FSR402 sensors (period: FSR_INTERVAL_MS). */
#ifndef GAIT_PLANTAR_THREAD_H_
#define GAIT_PLANTAR_THREAD_H_

/* Start the periodic FSR thread. Call after fsr402_init() succeeded. Returns 0. */
int plantar_thread_start(void);

/*
 * Consumer side of the FSR FIFO queue (capacity FSR_QUEUE_DEPTH).
 * Copies the oldest sample into *s. Returns 0, or -EAGAIN / -ENOMSG on timeout.
 */
#include <zephyr/kernel.h>
#include "data/gait_data.h"

int plantar_queue_get(struct plantar_sample *s, k_timeout_t timeout);

#endif /* GAIT_PLANTAR_THREAD_H_ */
