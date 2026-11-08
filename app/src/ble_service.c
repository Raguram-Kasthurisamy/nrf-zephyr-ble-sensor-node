/*
 * Author: Raguram Kasthurisamy
 *
 * Bluetooth LE peripheral with one custom GATT service:
 *   telemetry  read + notify   needs an encrypted link to read or to subscribe
 *   command    write           needs an authenticated (passkey) pairing
 *   interval   read + write    write needs an authenticated pairing
 * Pairing uses LE Secure Connections only. The node displays a passkey (here: on the log),
 * the phone user types it, which protects against man-in-the-middle attacks; the bond is
 * stored in flash so the phone reconnects without pairing again.
 */
#include "ble_service.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/zbus/zbus.h>

#include "app_events.h"
#include "channels.h"
#include "node_config.h"
#include "sensors.h"
#include "telemetry.h"

LOG_MODULE_REGISTER(ble, LOG_LEVEL_INF);

#define NODE_SERVICE_UUID_VAL   BT_UUID_128_ENCODE(0x9a3b0001, 0x6c0e, 0x4b6f, 0x8e8a, 0x2f5d1c7b4e10)
#define TELEMETRY_CHAR_UUID_VAL BT_UUID_128_ENCODE(0x9a3b0002, 0x6c0e, 0x4b6f, 0x8e8a, 0x2f5d1c7b4e10)
#define COMMAND_CHAR_UUID_VAL   BT_UUID_128_ENCODE(0x9a3b0003, 0x6c0e, 0x4b6f, 0x8e8a, 0x2f5d1c7b4e10)
#define INTERVAL_CHAR_UUID_VAL  BT_UUID_128_ENCODE(0x9a3b0004, 0x6c0e, 0x4b6f, 0x8e8a, 0x2f5d1c7b4e10)

static const struct bt_uuid_128 node_service_uuid = BT_UUID_INIT_128(NODE_SERVICE_UUID_VAL);
static const struct bt_uuid_128 telemetry_uuid = BT_UUID_INIT_128(TELEMETRY_CHAR_UUID_VAL);
static const struct bt_uuid_128 command_uuid = BT_UUID_INIT_128(COMMAND_CHAR_UUID_VAL);
static const struct bt_uuid_128 interval_uuid = BT_UUID_INIT_128(INTERVAL_CHAR_UUID_VAL);

enum node_command {
	NODE_CMD_SAMPLE_NOW = 0x01,
	NODE_CMD_IDENTIFY = 0x02,
};

static struct k_spinlock telemetry_lock;
static uint8_t telemetry_value[TELEMETRY_PACKET_SIZE]; /* guarded by telemetry_lock */
static bool notify_enabled;
static struct bt_conn *current_conn;
static struct k_work notify_work;
static struct k_work adv_work;

/* ---- GATT service ------------------------------------------------------------------- */

static void telemetry_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	notify_enabled = value == BT_GATT_CCC_NOTIFY;
	LOG_INF("telemetry notifications %s", notify_enabled ? "on" : "off");
}

static ssize_t read_telemetry(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			      uint16_t len, uint16_t offset)
{
	uint8_t copy[TELEMETRY_PACKET_SIZE];
	k_spinlock_key_t key = k_spin_lock(&telemetry_lock);

	memcpy(copy, telemetry_value, sizeof(copy));
	k_spin_unlock(&telemetry_lock, key);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, copy, sizeof(copy));
}

static ssize_t write_command(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0 || len != 1) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	switch (((const uint8_t *)buf)[0]) {
	case NODE_CMD_SAMPLE_NOW:
		sensors_request_sample();
		break;
	case NODE_CMD_IDENTIFY:
		app_post_event(APP_EVENT_IDENTIFY);
		break;
	default:
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	return len;
}

static ssize_t read_interval(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			     uint16_t len, uint16_t offset)
{
	const uint16_t value = sys_cpu_to_le16((uint16_t)node_config_interval_ms());

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &value, sizeof(value));
}

static ssize_t write_interval(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			      const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0 || len != sizeof(uint16_t)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	if (node_config_set_interval_ms(sys_get_le16(buf)) != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	return len;
}

/* Attribute order matters: index 2 is the telemetry value used for notifications. */
BT_GATT_SERVICE_DEFINE(node_svc,
	BT_GATT_PRIMARY_SERVICE(&node_service_uuid),
	BT_GATT_CHARACTERISTIC(&telemetry_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT, read_telemetry, NULL, NULL),
	BT_GATT_CCC(telemetry_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),
	BT_GATT_CHARACTERISTIC(&command_uuid.uuid, BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_WRITE_AUTHEN, NULL, write_command, NULL),
	BT_GATT_CHARACTERISTIC(&interval_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_AUTHEN,
			       read_interval, write_interval, NULL),
);

#define TELEMETRY_ATTR (&node_svc.attrs[2])

/* ---- zbus: telemetry in, notification out --------------------------------------------- */

/* Listeners run synchronously in the publisher's thread with the channel locked, so only
 * copy the data here; the notification itself goes out from the system work queue. */
static void on_telemetry(const struct zbus_channel *chan)
{
	const struct node_telemetry *telemetry = zbus_chan_const_msg(chan);
	uint8_t packet[TELEMETRY_PACKET_SIZE];

	(void)telemetry_encode(telemetry, packet);
	k_spinlock_key_t key = k_spin_lock(&telemetry_lock);

	memcpy(telemetry_value, packet, sizeof(packet));
	k_spin_unlock(&telemetry_lock, key);
	(void)k_work_submit(&notify_work);
}

ZBUS_LISTENER_DEFINE(ble_telemetry_listener, on_telemetry);

static void notify_work_handler(struct k_work *work)
{
	uint8_t copy[TELEMETRY_PACKET_SIZE];

	ARG_UNUSED(work);
	if (!notify_enabled || current_conn == NULL) {
		return;
	}
	k_spinlock_key_t key = k_spin_lock(&telemetry_lock);

	memcpy(copy, telemetry_value, sizeof(copy));
	k_spin_unlock(&telemetry_lock, key);
	const int err = bt_gatt_notify(NULL, TELEMETRY_ATTR, copy, sizeof(copy));

	if (err != 0 && err != -ENOTCONN) {
		LOG_WRN("notify failed (%d)", err);
	}
}

/* ---- Advertising ------------------------------------------------------------------------ */

static const struct bt_data advertising_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, NODE_SERVICE_UUID_VAL),
};

static const struct bt_data scan_response[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

int ble_start_advertising(void)
{
#ifdef CONFIG_APP_SLOW_ADVERTISING
	const struct bt_le_adv_param *param = BT_LE_ADV_PARAM(
		BT_LE_ADV_OPT_CONN, BT_GAP_ADV_SLOW_INT_MIN, BT_GAP_ADV_SLOW_INT_MAX, NULL);
#else
	const struct bt_le_adv_param *param = BT_LE_ADV_CONN_FAST_1;
#endif
	const int err = bt_le_adv_start(param, advertising_data, ARRAY_SIZE(advertising_data),
					scan_response, ARRAY_SIZE(scan_response));

	return err == -EALREADY ? 0 : err;
}

static void adv_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	const int err = ble_start_advertising();

	if (err != 0) {
		LOG_ERR("advertising restart failed (%d)", err);
	}
}

/* ---- Connection and security callbacks --------------------------------------------------- */

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0) {
		LOG_WRN("connection failed (0x%02x)", err);
		return;
	}
	current_conn = bt_conn_ref(conn);
	LOG_INF("connected");
	app_post_event(APP_EVENT_CONNECTED);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	LOG_INF("disconnected (reason 0x%02x)", reason);
	if (current_conn != NULL) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}
	notify_enabled = false;
	app_post_event(APP_EVENT_DISCONNECTED);
}

/* The connection object has been freed: only now can advertising start again. */
static void recycled(void)
{
	(void)k_work_submit(&adv_work);
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	ARG_UNUSED(conn);
	if (err != 0) {
		LOG_WRN("security failed: level %u, error %d", level, err);
	} else {
		LOG_INF("security level %u", level);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
	.security_changed = security_changed,
};

static void passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	ARG_UNUSED(conn);
	LOG_INF("pairing passkey: %06u (enter it on the phone)", passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
	ARG_UNUSED(conn);
	LOG_INF("pairing cancelled");
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	ARG_UNUSED(conn);
	LOG_INF("pairing complete, %s", bonded ? "bonded" : "not bonded");
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	ARG_UNUSED(conn);
	LOG_WRN("pairing failed (%d)", reason);
}

static struct bt_conn_auth_cb auth_callbacks = {
	.passkey_display = passkey_display,
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

/* ---- Public API ------------------------------------------------------------------------------ */

int ble_init(void)
{
	int err;

	k_work_init(&notify_work, notify_work_handler);
	k_work_init(&adv_work, adv_work_handler);

	err = bt_conn_auth_cb_register(&auth_callbacks);
	if (err != 0) {
		return err;
	}
	err = bt_conn_auth_info_cb_register(&auth_info_callbacks);
	if (err != 0) {
		return err;
	}
	err = bt_enable(NULL);
	if (err != 0) {
		return err;
	}
	if (IS_ENABLED(CONFIG_SETTINGS)) {
		err = settings_load(); /* bonds, identity address, and node/ settings */
	}
	return err;
}

bool ble_is_connected(void)
{
	return current_conn != NULL;
}
