/*
 * Mock producer: replaces imu_thread + plantar_thread when CONFIG_GAIT_MOCK_DATA=y.
 * Plantar samples go through the SAME queue (data_queue) and ble_thread as real data,
 * so the whole BLE path is exercised. IMU goes straight to gait_ble (it has no queue yet).
 */
#ifndef MOCK_THREAD_H_
#define MOCK_THREAD_H_

int mock_thread_start(void);

#endif /* MOCK_THREAD_H_ */
