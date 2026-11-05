/*
 * BLE streaming module. Owner: Nordic.
 *
 * Placeholder: returns -ENOTSUP until the link is proven in experiments/ble_hello
 * and the packet layout in protocol/BLE_GAIT_PROTOCOL.md is agreed.
 */
#include <zephyr/kernel.h>
#include <errno.h>

#include "ble/gait_ble.h"

int gait_ble_init(void)
{
	/* TODO(nordic): bt_enable(), register GATT service, start advertising */
	return -ENOTSUP;
}

int gait_ble_send_imu(const struct gait_imu_sample *s)
{
	ARG_UNUSED(s);
	/* TODO(nordic): pack per protocol doc and bt_gatt_notify() */
	return -ENOTSUP;
}

int gait_ble_send_plantar(const struct gait_plantar_sample *s)
{
	ARG_UNUSED(s);
	return -ENOTSUP;
}
