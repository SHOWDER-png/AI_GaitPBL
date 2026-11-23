/*
 * ble_hello: first BLE link Thingy:53 -> laptop / i.MX95.
 *
 * Advertises as "GaitNode" and, once a central subscribes, notifies an 18-byte
 * packet at 100 Hz:
 *
 *   offset 0  uint16  sequence number (wraps)
 *   offset 2  uint32  t_ms, Thingy uptime
 *   offset 6  12 bytes of filler
 *
 * 18 bytes is the size of the IMU packet proposed in protocol/BLE_GAIT_PROTOCOL.md,
 * so the loss and throughput measured here apply to the real packet.
 *
 * The sequence number advances once per 10 ms tick whether or not the packet
 * could be sent, so every packet the Thingy failed to send shows up as a gap
 * on the receiver.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <stdio.h>
#include <string.h>

#define SAMPLE_PERIOD_MS 10 /* 100 Hz */
#define PACKET_LEN       18

/*
 * Experiment-only UUIDs (random). The receiver script uses the same values.
 * Service:        b7b7a76b-f857-45e2-84d3-4bae85442671
 * Counter (notify): b7b7a76c-f857-45e2-84d3-4bae85442671
 */
#define HELLO_SVC_UUID_VAL \
	BT_UUID_128_ENCODE(0xb7b7a76b, 0xf857, 0x45e2, 0x84d3, 0x4bae85442671)
#define HELLO_CNT_UUID_VAL \
	BT_UUID_128_ENCODE(0xb7b7a76c, 0xf857, 0x45e2, 0x84d3, 0x4bae85442671)

static const struct bt_uuid_128 hello_svc_uuid = BT_UUID_INIT_128(HELLO_SVC_UUID_VAL);
static const struct bt_uuid_128 hello_cnt_uuid = BT_UUID_INIT_128(HELLO_CNT_UUID_VAL);

static volatile bool subscribed;

static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	subscribed = (value == BT_GATT_CCC_NOTIFY);
	printf("notifications %s\n", subscribed ? "ON" : "OFF");
}

/* attrs[0] service, attrs[1] characteristic declaration, attrs[2] value, attrs[3] CCC */
BT_GATT_SERVICE_DEFINE(hello_svc,
	BT_GATT_PRIMARY_SERVICE(&hello_svc_uuid),
	BT_GATT_CHARACTERISTIC(&hello_cnt_uuid.uuid, BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE, NULL, NULL, NULL),
	BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* Advertising packet: flags + service UUID. Name goes in the scan response
 * because a 128-bit UUID plus "GaitNode" does not fit in 31 bytes together.
 */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, HELLO_SVC_UUID_VAL),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void adv_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err && err != -EALREADY) {
		printf("advertising failed to start (err %d)\n", err);
		return;
	}
	printf("advertising as %s\n", CONFIG_BT_DEVICE_NAME);
}

static K_WORK_DEFINE(adv_work, adv_work_handler);

static void connected(struct bt_conn *conn, uint8_t err)
{
	ARG_UNUSED(conn);
	if (err) {
		printf("connection failed (err 0x%02x)\n", err);
		return;
	}
	printf("connected\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	subscribed = false;
	printf("disconnected (reason 0x%02x)\n", reason);
}

/* The connection object is free again: now it is safe to advertise for the next central. */
static void recycled(void)
{
	k_work_submit(&adv_work);
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
			     uint16_t timeout)
{
	ARG_UNUSED(conn);
	/* interval is in units of 1.25 ms */
	printf("conn params: interval %u.%02u ms, latency %u, timeout %u ms\n",
	       (interval * 125U) / 100U, (interval * 125U) % 100U, latency, timeout * 10U);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
	.le_param_updated = le_param_updated,
};

static K_TIMER_DEFINE(sample_timer, NULL, NULL);

int main(void)
{
	uint8_t pkt[PACKET_LEN];
	uint16_t seq = 0;
	uint32_t sent = 0, failed = 0, skipped = 0, ticks = 0;
	bool was_subscribed = false;
	int err;

	err = bt_enable(NULL);
	if (err) {
		printf("bt_enable failed (err %d)\n", err);
		return 0;
	}
	k_work_submit(&adv_work);

	k_timer_start(&sample_timer, K_MSEC(SAMPLE_PERIOD_MS), K_MSEC(SAMPLE_PERIOD_MS));

	while (1) {
		/* Sleeps until the next 10 ms tick. Returns how many ticks passed since
		 * the last call: more than 1 means we were stuck (e.g. BLE buffers full).
		 */
		uint32_t n = k_timer_status_sync(&sample_timer);

		if (!subscribed) {
			was_subscribed = false;
			continue;
		}
		if (!was_subscribed) {
			/* New run: start counting from 0 */
			was_subscribed = true;
			seq = 0;
			sent = failed = skipped = ticks = 0;
			n = 1;
		}

		seq += n - 1; /* ticks we missed become gaps in the sequence */
		skipped += n - 1;

		sys_put_le16(seq, &pkt[0]);
		sys_put_le32(k_uptime_get_32(), &pkt[2]);
		memset(&pkt[6], 0xA5, PACKET_LEN - 6);

		err = bt_gatt_notify(NULL, &hello_svc.attrs[1], pkt, sizeof(pkt));
		if (err) {
			failed++;
		} else {
			sent++;
		}
		seq++;

		ticks += n;
		if (ticks >= 1000 / SAMPLE_PERIOD_MS) {
			ticks = 0;
			printf("seq=%u sent=%u failed=%u skipped=%u\n", seq, sent, failed, skipped);
		}
	}
	return 0;
}
