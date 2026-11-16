/* IMU thread: decides WHEN to sample the BMI270 (period: IMU_INTERVAL_MS). */
#ifndef GAIT_IMU_THREAD_H_
#define GAIT_IMU_THREAD_H_

/* Start the periodic IMU thread. Call after imu_init() succeeded. Returns 0. */
int imu_thread_start(void);

#endif /* GAIT_IMU_THREAD_H_ */
