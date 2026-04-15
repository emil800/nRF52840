#include "ble.h"

#include <errno.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

static volatile bool ble_connected;
static volatile bool leds_active;
static volatile enum bike_state current_mode = STATE_FORWARD;

static K_SEM_DEFINE(wake_sem, 0, 16);

static struct bt_uuid_128 bike_svc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x567812345678));

static struct bt_uuid_128 bike_char_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x567812345679));

static ssize_t write_bike_mode(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			       const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(offset);
	ARG_UNUSED(flags);

	if (len < 1) {
		return len;
	}

	const uint8_t *value = buf;

	switch (value[0]) {
	case 0:
		leds_active = false;
		break;
	case 1:
		leds_active = true;
		current_mode = STATE_FORWARD;
		break;
	case 2:
		leds_active = true;
		current_mode = STATE_LEFT;
		break;
	case 3:
		leds_active = true;
		current_mode = STATE_RIGHT;
		break;
	default:
		break;
	}

	k_sem_give(&wake_sem);
	return len;
}

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL,
		      BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x567812345678)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/*
 * BLE advertising interval unit = 0.625 ms
 * 5000 ms / 0.625 = 8000 = 0x1F40
 */
static const struct bt_le_adv_param slow_adv_params =
	BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONN, 0x1F40, 0x1F40, NULL);

static void restart_adv_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(restart_adv_work, restart_adv_work_handler);

static void switch_to_slow_adv(struct k_work *work)
{
	ARG_UNUSED(work);
	(void)bt_le_adv_stop();
	(void)bt_le_adv_start(&slow_adv_params, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
}

static K_WORK_DELAYABLE_DEFINE(slow_adv_work, switch_to_slow_adv);

static void restart_adv_work_handler(struct k_work *work)
{
	int err;
	ARG_UNUSED(work);

	(void)bt_le_adv_stop();
	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err == 0 || err == -EALREADY) {
		/* start fast, schedule slowdown after 30s */
		(void)k_work_schedule(&slow_adv_work, K_SECONDS(30));
		return;
	}

	(void)k_work_schedule(&restart_adv_work, K_MSEC(250));
}

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	ARG_UNUSED(conn);

	if (err == 0) {
		ble_connected = true;
		k_sem_give(&wake_sem);
	}
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(reason);

	ble_connected = false;
	leds_active = false;
	k_sem_give(&wake_sem);
	(void)k_work_schedule(&restart_adv_work, K_MSEC(150));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
};

BT_GATT_SERVICE_DEFINE(bike_svc,
	BT_GATT_PRIMARY_SERVICE(&bike_svc_uuid),
	BT_GATT_CHARACTERISTIC(&bike_char_uuid.uuid,
			       BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_WRITE,
			       NULL, write_bike_mode, NULL),
);

int ble_init(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		return err;
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		return err;
	}

	/* start fast, schedule slowdown after 30s */
	(void)k_work_schedule(&slow_adv_work, K_SECONDS(30));

	return 0;
}

bool ble_is_active(void)
{
	return ble_connected && leds_active;
}

void ble_wait_for_active(void)
{
	while (!ble_is_active()) {
		(void)k_sem_take(&wake_sem, K_FOREVER);
	}
}

enum bike_state ble_get_mode(void)
{
	return current_mode;
}

