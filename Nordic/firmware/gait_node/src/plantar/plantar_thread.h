/* Plantar thread: decides WHEN to read the six FSR402 sensors (period: FSR_INTERVAL_MS).
 * Samples are pushed into the shared queue (data/data_queue.h); ble_thread consumes them.
 */
#ifndef GAIT_PLANTAR_THREAD_H_
#define GAIT_PLANTAR_THREAD_H_

/* Start the periodic FSR thread. Call after fsr402_init() succeeded. Returns 0 or -ENOMEM. */
int plantar_thread_start(void);

#endif /* GAIT_PLANTAR_THREAD_H_ */
