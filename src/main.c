#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <hal/nrf_i2s.h>
#include "battery_monitor.h"

/*
 * After a soft reset, peripheral PSEL registers can persist. The board's
 * default SPI2 uses the same pins as our I2S output, so disconnect both
 * before driver init to leave the pin mux in a clean state.
 */
static int peripheral_force_reset(void)
{
	nrf_i2s_task_trigger(NRF_I2S0, NRF_I2S_TASK_STOP);
	nrf_i2s_disable(NRF_I2S0);

	NRF_SPIM2->ENABLE = 0;
	NRF_SPIM2->PSEL.SCK = 0xFFFFFFFFUL;
	NRF_SPIM2->PSEL.MOSI = 0xFFFFFFFFUL;
	NRF_SPIM2->PSEL.MISO = 0xFFFFFFFFUL;

	NRF_SPI2->ENABLE = 0;
	NRF_SPI2->PSEL.SCK = 0xFFFFFFFFUL;
	NRF_SPI2->PSEL.MOSI = 0xFFFFFFFFUL;
	NRF_SPI2->PSEL.MISO = 0xFFFFFFFFUL;

	return 0;
}

SYS_INIT(peripheral_force_reset, PRE_KERNEL_1, 0);

/* --- LED Matrix Configuration --- */
#define STRIP_NODE DT_ALIAS(led_strip)
#define WIDTH  16
#define HEIGHT 16
#define NUM_PIXELS (WIDTH * HEIGHT)

#define SLEEP_MS_ACTIVE 100
#define FWD_R 32
#define FWD_G 32
#define FWD_B 32
#define AMBER_R 64
#define AMBER_G 32
#define AMBER_B 0

enum bike_state { STATE_FORWARD = 0, STATE_LEFT = 1, STATE_RIGHT = 2 };

static volatile bool ble_connected;
static volatile bool leds_active;
static volatile enum bike_state current_mode = STATE_FORWARD;

static K_SEM_DEFINE(wake_sem, 0, 16);

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[NUM_PIXELS];

/* --- Bluetooth Service Logic --- */

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

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	ARG_UNUSED(conn);

	if (err == 0) {
		ble_connected = true;
		k_sem_give(&wake_sem);
	}
}

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL,
		      BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x567812345678)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};
/* BLE advertising interval unit = 0.625 ms
 * 5000 ms / 0.625 = 8000 = 0x1F40
 */
static const struct bt_le_adv_param slow_adv_params =
    BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONN,
                         0x1F40,   /* min interval = 5000ms */
                         0x1F40,   /* max interval = 5000ms */
                         NULL);

static void restart_adv_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(restart_adv_work, restart_adv_work_handler);

static void switch_to_slow_adv(struct k_work *work)
{
    ARG_UNUSED(work);
    bt_le_adv_stop();
    bt_le_adv_start(&slow_adv_params, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
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
        k_work_schedule(&slow_adv_work, K_SECONDS(30));
        return;
    }

    (void)k_work_schedule(&restart_adv_work, K_MSEC(250));
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

/* --- Animation Functions --- */

static uint32_t get_idx(int x, int y)
{
	if (y % 2 != 0) {
		return (uint32_t)((y * WIDTH) + (WIDTH - 1 - x));
	}
	return (uint32_t)((y * WIDTH) + x);
}

static void plot(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
	if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) {
		return;
	}
	struct led_rgb *p = &pixels[get_idx(x, y)];

	p->r = r;
	p->g = g;
	p->b = b;
}

static int wrap_y(int y)
{
	y %= HEIGHT;
	if (y < 0) {
		y += HEIGHT;
	}
	return y;
}

static int wrap_x(int x)
{
	x %= WIDTH;
	if (x < 0) {
		x += WIDTH;
	}
	return x;
}

static void blit_up_arrow(int origin_y, uint8_t r, uint8_t g, uint8_t b)
{
	const int tip = origin_y;

	plot(7, wrap_y(tip), r, g, b);
	plot(8, wrap_y(tip), r, g, b);
	for (int x = 6; x <= 9; x++) {
		plot(x, wrap_y(tip + 1), r, g, b);
	}
	for (int x = 5; x <= 10; x++) {
		plot(x, wrap_y(tip + 2), r, g, b);
	}
	for (int dy = 3; dy < 11; dy++) {
		plot(7, wrap_y(tip + dy), r, g, b);
		plot(8, wrap_y(tip + dy), r, g, b);
	}
}

static void blit_left_arrow(int origin_x, uint8_t r, uint8_t g, uint8_t b)
{
	const int tip = origin_x;

	plot(wrap_x(tip), 7, r, g, b);
	plot(wrap_x(tip), 8, r, g, b);
	for (int y = 6; y <= 9; y++) {
		plot(wrap_x(tip + 1), y, r, g, b);
	}
	for (int y = 5; y <= 10; y++) {
		plot(wrap_x(tip + 2), y, r, g, b);
	}
	for (int dx = 3; dx < 11; dx++) {
		plot(wrap_x(tip + dx), 7, r, g, b);
		plot(wrap_x(tip + dx), 8, r, g, b);
	}
}

static void blit_right_arrow(int origin_x, uint8_t r, uint8_t g, uint8_t b)
{
	const int tip = origin_x;

	plot(wrap_x(tip), 7, r, g, b);
	plot(wrap_x(tip), 8, r, g, b);
	for (int y = 6; y <= 9; y++) {
		plot(wrap_x(tip - 1), y, r, g, b);
	}
	for (int y = 5; y <= 10; y++) {
		plot(wrap_x(tip - 2), y, r, g, b);
	}
	for (int dx = 3; dx < 11; dx++) {
		plot(wrap_x(tip - dx), 7, r, g, b);
		plot(wrap_x(tip - dx), 8, r, g, b);
	}
}

void draw_forward(uint32_t frame)
{
	memset(pixels, 0, sizeof(pixels));
	int origin_y = -(int)(frame % HEIGHT);

	blit_up_arrow(origin_y, FWD_R, FWD_G, FWD_B);
}

void draw_left(uint32_t frame)
{
	memset(pixels, 0, sizeof(pixels));
	int origin_x = -(int)(frame % WIDTH);

	blit_left_arrow(origin_x, AMBER_R, AMBER_G, AMBER_B);
}

void draw_right(uint32_t frame)
{
	memset(pixels, 0, sizeof(pixels));
	int origin_x = (int)(frame % WIDTH);

	blit_right_arrow(origin_x, AMBER_R, AMBER_G, AMBER_B);
}



static void battery_check_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(battery_check_work, battery_check_work_handler);

static void battery_check_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	int mv = battery_monitor_read_mv();
	battery_monitor_update_leds(mv);
	if (mv < 0) {
		printk("Battery read failed: %d\n", mv);
	} else {
		printk("Battery: %d mV\n", mv);
	}

	(void)k_work_schedule(&battery_check_work, K_SECONDS(60));
}

/* --- Main Loop --- */

int main(void)
{
	int err;
	uint32_t frame = 0;


	/* Init LED strip */
	if (!device_is_ready(strip)) {
		return -1;
	}

	/* Initialize Bluetooth */
	err = bt_enable(NULL);
	if (err) {
		return -1;
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad),
	sd, ARRAY_SIZE(sd));

	if (err) {
		return -1;
	}

	/* start fast, schedule slowdown after 30s */
	k_work_schedule(&slow_adv_work, K_SECONDS(30));

	/* Initialize battery monitor */
	err = battery_monitor_init();
	if (err) {
		return -1;
	}
	(void)k_work_schedule(&battery_check_work, K_SECONDS(5));

	while (1) {
		if (!(ble_connected && leds_active)) {
			memset(pixels, 0, sizeof(pixels));
			(void)led_strip_update_rgb(strip, pixels, NUM_PIXELS);
			frame = 0;
			k_sem_take(&wake_sem, K_FOREVER);
			continue;
		}

		switch (current_mode) {
		case STATE_LEFT:
			draw_left(frame);
			break;
		case STATE_RIGHT:
			draw_right(frame);
			break;
		default:
			draw_forward(frame);
			break;
		}

		(void)led_strip_update_rgb(strip, pixels, NUM_PIXELS);
		frame++;
		k_msleep(SLEEP_MS_ACTIVE);
	}
	return 0;
}
