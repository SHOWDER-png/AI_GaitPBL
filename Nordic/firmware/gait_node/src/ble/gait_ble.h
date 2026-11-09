/* BLE streaming module. Owner: Nordic. Packet layout: protocol/BLE_GAIT_PROTOCOL.md */
#ifndef GAIT_BLE_H_
#define GAIT_BLE_H_

#include "common/gait_types.h"

/* Enable Bluetooth, register the gait GATT service and start advertising. */
int gait_ble_init(void);

/* Queue one sample for notification. Return -ENOTCONN if no central is subscribed. */
int gait_ble_send_imu(const struct gait_imu_sample *s);
int gait_ble_send_plantar(const struct gait_plantar_sample *s);

#endif /* GAIT_BLE_H_ */
