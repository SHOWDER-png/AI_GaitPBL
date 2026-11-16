/*
 * BLE streaming module. Owner: Nordic.
 *
 * The Thingy:53 is a BLE peripheral advertising as "GaitNode". It exposes one
 * service with two notify characteristics (IMU, plantar). Packet layout and UUIDs:
 * protocol/BLE_GAIT_PROTOCOL.md. Link code comes from experiments/ble_hello.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <errno.h>
#include <stdio.h>

#include "ble/gait_ble.h"

#define GAIT_PACKET_LEN 18

/*
 * Service: e6d1e9d9-be0e-4889-b490-7643045692db
 * IMU:     e6d1e9da-be0e-4889-b490-7643045692db
 * Plantar: e6d1e9db-be0e-4889-b490-7643045692db
 */
#define GAIT_SVC_UUID_VAL \
	BT_UUID_128_ENCODE(0xe6d1e9d9, 0xbe0e, 0x4889, 0xb490, 0x7643045692db)
#define GAIT_IMU_UUID_VAL \
	BT_UUID_128_ENCODE(0xe6d1e9da, 0xbe0e, 0x4889, 0xb490, 0x7643045692db)
#define GAIT_PLANTAR_UUID_VAL \
	BT_UUID_128_ENCODE(0xe6d1e9db, 0xbe0e, 0x4889, 0xb490, 0x7643045692db)

static const struct bt_uuid_128 gait_svc_uuid = BT_UUID_INIT_128(GAIT_SVC_UUID_VAL);
static const struct bt_uuid_128 gait_imu_uuid = BT_UUID_INIT_128(GAIT_IMU_UUID_VAL);
static const struct bt_uuid_128 gait_plantar_uuid = BT_UUID_INIT_128(GAIT_PLANTAR_UUID_VAL);

/* Set when the central switches notifications on for that characteristic */
static volatile bool imu_subscribed;
static volatile bool plantar_subscribed;

static uint16_t imu_seq;
static uint16_t plantar_seq;

static void imu_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	imu_subscribed = (value == BT_GATT_CCC_NOTIFY);
	printf("BLE: IMU notifications %s\n", imu_subscribed ? "ON" : "OFF");
}

static void plantar_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	plantar_subscribed = (value == BT_GATT_CCC_NOTIFY);
	printf("BLE: plantar notifications %s\n", plantar_subscribed ? "ON" : "OFF");
}

/*
 * Attribute table index:
 *   [0] service
 *   [1] IMU characteristic declaration   [2] IMU value      [3] IMU CCC
 *   [4] plantar characteristic declaration [5] plantar value [6] plantar CCC
 */
BT_GATT_SERVICE_DEFINE(gait_svc,
	BT_GATT_PRIMARY_SERVICE(&gait_svc_uuid),
	BT_GATT_CHARACTERISTIC(&gait_imu_uuid.uuid, BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE, NULL, NULL, NULL),
	BT_GATT_CCC(imu_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&gait_plantar_uuid.uuid, BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE, NULL, NULL, NULL),
	BT_GATT_CCC(plantar_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

#define IMU_ATTR     (&gait_svc.attrs[1])
#define PLANTAR_ATTR (&gait_svc.attrs[4])

/* Name goes in the scan response: a 128-bit UUID plus the name does not fit in 31 bytes. */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, GAIT_SVC_UUID_VAL),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void adv_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err && err != -EALREADY) {
		printf("BLE: advertising failed to start (err %d)\n", err);
		return;
	}
	printf("BLE: advertising as %s\n", CONFIG_BT_DEVICE_NAME);
}

static K_WORK_DEFINE(adv_work, adv_work_handler);

static void connected(struct bt_conn *conn, uint8_t err)
{
	ARG_UNUSED(conn);
	if (err) {
		printf("BLE: connection failed (err 0x%02x)\n", err);
		return;
	}
	printf("BLE: connected\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	imu_subscribed = false;
	plantar_subscribed = false;
	printf("BLE: disconnected (reason 0x%02x)\n", reason);
}

/* The connection object is free again: advertise for the next central. */
static void recycled(void)
{
	k_work_submit(&adv_work);
}

BT_CONN_CB_DEFINE(gait_conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
};

int gait_ble_init(void)
{
	int err = bt_enable(NULL);

	if (err) {
		return err;
	}
	k_work_submit(&adv_work);
	return 0;
}

/* float in SI units -> int16 in 0.001 units, clamped instead of wrapping */
static int16_t to_milli_i16(float v)
{
	float scaled = v * 1000.0f;

	return (int16_t)CLAMP(scaled, -32768.0f, 32767.0f);
}

int gait_ble_send_imu(const struct gait_imu_sample *s)
{
	uint8_t pkt[GAIT_PACKET_LEN];

	if (!imu_subscribed) {
		return -ENOTCONN;
	}

	sys_put_le16(imu_seq++, &pkt[0]);
	sys_put_le32(s->t_ms, &pkt[2]);
	for (int i = 0; i < 3; i++) {
		sys_put_le16((uint16_t)to_milli_i16(s->acc[i]), &pkt[6 + 2 * i]);
		sys_put_le16((uint16_t)to_milli_i16(s->gyr[i]), &pkt[12 + 2 * i]);
	}

	return bt_gatt_notify(NULL, IMU_ATTR, pkt, sizeof(pkt));
}

int gait_ble_send_plantar(const struct gait_plantar_sample *s)
{
	uint8_t pkt[GAIT_PACKET_LEN];

	if (!plantar_subscribed) {
		return -ENOTCONN;
	}

	sys_put_le16(plantar_seq++, &pkt[0]);
	sys_put_le32(s->t_ms, &pkt[2]);
	for (int i = 0; i < GAIT_FSR_COUNT; i++) {
		sys_put_le16((uint16_t)s->raw[i], &pkt[6 + 2 * i]);
	}

	return bt_gatt_notify(NULL, PLANTAR_ATTR, pkt, sizeof(pkt));
}
