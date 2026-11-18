/* BLE consumer thread: drains the FSR queue and sends one notification per sample. */
#ifndef GAIT_BLE_THREAD_H_
#define GAIT_BLE_THREAD_H_

/* Start after gait_ble_init() and fsr402_init() both succeeded. Returns 0. */
int ble_thread_start(void);

#endif /* GAIT_BLE_THREAD_H_ */
