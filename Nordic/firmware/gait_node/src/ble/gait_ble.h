/* BLE streaming module. Owner: Nordic. Packet layout: protocol/BLE_GAIT_PROTOCOL.md */
#ifndef GAIT_BLE_H_
#define GAIT_BLE_H_

#include <stdbool.h>
#include "common/gait_types.h"

/* Commands written by the central to the Control characteristic (1 byte) */
enum gait_cmd { GAIT_CMD_STOP = 0, GAIT_CMD_START = 1, GAIT_CMD_CALIBRATE = 2 };

typedef void (*gait_cmd_cb_t)(enum gait_cmd cmd);

/* Enable Bluetooth, register the gait GATT service and start advertising. */
int gait_ble_init(void);

/* Optional: get told when the central writes a command (called from the BT thread). */
void gait_ble_set_cmd_callback(gait_cmd_cb_t cb);

/* false after the central wrote STOP; send functions then return -EPERM. */
bool gait_ble_streaming(void);

/* Queue one sample for notification. Return -ENOTCONN if no central is subscribed,
 * -EPERM if the central stopped streaming. */
int gait_ble_send_imu(const struct gait_imu_sample *s);
int gait_ble_send_plantar(const struct gait_plantar_sample *s);

#endif /* GAIT_BLE_H_ */
