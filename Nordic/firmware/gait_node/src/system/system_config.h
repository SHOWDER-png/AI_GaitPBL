/*
 * Timing configuration for the gait node. The ONLY place where sampling rates,
 * thread priorities and stack sizes are defined.
 *
 * Starting values: measure on real hardware and tune.
 */
#ifndef SYSTEM_CONFIG_H_
#define SYSTEM_CONFIG_H_

/* ---- Rates (Hz) ---- */
#define IMU_SAMPLE_RATE_HZ 100
#define FSR_SAMPLE_RATE_HZ 100
#define BLE_SEND_RATE_HZ   100 /* target: 1 notification per FSR sample */

/* ---- Periods (ms), derived. Do not define them anywhere else. ---- */
#define IMU_INTERVAL_MS (1000 / IMU_SAMPLE_RATE_HZ)
#define FSR_INTERVAL_MS (1000 / FSR_SAMPLE_RATE_HZ)
#define BLE_INTERVAL_MS (1000 / BLE_SEND_RATE_HZ)

/* ---- Queues ---- */
#define FSR_QUEUE_DEPTH 20 /* samples = 200 ms of FSR data at 100 Hz */

/* ---- Thread priorities: lower number = more important ---- */
#define IMU_THREAD_PRIORITY 4
#define FSR_THREAD_PRIORITY 5
#define BLE_THREAD_PRIORITY 6

/* ---- Stack sizes (bytes) ----
 * IMU and FSR are 4096 instead of 2048 because both threads still printf()
 * (floats for the IMU) until the log thread exists (phase 6).
 * If a thread crashes or the board resets, raise that thread's stack.
 */
#define IMU_STACK_SIZE 4096
#define FSR_STACK_SIZE 4096
#define BLE_STACK_SIZE 2048

#endif /* SYSTEM_CONFIG_H_ */
