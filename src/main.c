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

/* --- LED Matrix Configuration --- */
#define STRIP_NODE DT_ALIAS(led_strip)
#define WIDTH  16
#define HEIGHT 16
#define NUM_PIXELS (WIDTH * HEIGHT)

#define SLEEP_MS_ACTIVE 100
#define SLEEP_MS_LOWPOWER 5000

enum bike_state { STATE_FORWARD = 0, STATE_LEFT = 1, STATE_RIGHT = 2 };

/* After POR: no BLE link, LEDs off until connected and a 1/2/3 command enables them. */
static volatile bool ble_connected;
static volatile bool leds_active;
static volatile enum bike_state current_mode = STATE_FORWARD;

/* Wakes main loop immediately on state changes (connect, disconnect, command). */
static K_SEM_DEFINE(wake_sem, 0, 1);

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[NUM_PIXELS];

/* --- Bluetooth Service Logic --- */

// Custom Service UUID: 12345678-1234-5678-1234-567812345678
static struct bt_uuid_128 bike_svc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x567812345678));

// Characteristic UUID: 12345678-1234-5678-1234-567812345679
static struct bt_uuid_128 bike_char_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x567812345679));

/*
 * GATT write: 0 = LEDs off + low-power cadence; 1/2/3 = show forward / left / right
 * (only meaningful while connected; disconnect also forces LEDs off).
 */
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

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(reason);

	ble_connected = false;
	leds_active = false;
	k_sem_give(&wake_sem);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
};

// Define the Bluetooth Service
BT_GATT_SERVICE_DEFINE(bike_svc,
	BT_GATT_PRIMARY_SERVICE(&bike_svc_uuid),
	BT_GATT_CHARACTERISTIC(&bike_char_uuid.uuid,
			       BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_WRITE,
			       NULL, write_bike_mode, NULL),
);

// Bluetooth Advertising Data
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL,
		      BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x567812345678)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* --- Animation Functions --- */

static uint32_t get_idx(int x, int y)
{
	/* Zig-zag wiring common in 16x16 matrices */
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

/* Up arrow (↑), same geometry as forward: tip + flare + shaft (mirrors blit_left/right layout). */
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

/*
 * Left arrow (←): same proportions as up arrow, rotated — tip is the left column,
 * then wider columns, then an 8-column horizontal shaft on rows 7–8.
 */
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

/* Right arrow (→): mirror of left; tip is the right column of the head. */
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
	/* Scroll tip upward (negative origin moves pattern up the display) */
	int origin_y = -(int)(frame % HEIGHT);

	blit_up_arrow(origin_y, 2, 2, 2);
}

void draw_left(uint32_t frame)
{
	memset(pixels, 0, sizeof(pixels));
	/* Same scroll feel as forward, horizontal; amber */
	int origin_x = -(int)(frame % WIDTH);

	blit_left_arrow(origin_x, 16, 8, 0);
}

void draw_right(uint32_t frame)
{
	memset(pixels, 0, sizeof(pixels));
	/* Shaft leads left from tip; scroll right with +frame */
	int origin_x = (int)(frame % WIDTH);

	blit_right_arrow(origin_x, 16, 8, 0);
}

/* --- Main Loop --- */

int main(void) {
	int err;
	uint32_t frame = 0;

	if (!device_is_ready(strip)) return -1;

	// Initialize Bluetooth
	err = bt_enable(NULL);
	if (err) return -1;

	// Start Advertising
	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) return -1;

	/* Clear LEDs once at startup */
	memset(pixels, 0, sizeof(pixels));
	led_strip_update_rgb(strip, pixels, NUM_PIXELS);

	while (1) {
		if (!(ble_connected && leds_active)) {
			/* Low-power: LEDs off, block until a BLE event wakes us. */
			memset(pixels, 0, sizeof(pixels));
			led_strip_update_rgb(strip, pixels, NUM_PIXELS);
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

		led_strip_update_rgb(strip, pixels, NUM_PIXELS);
		frame++;
		k_msleep(SLEEP_MS_ACTIVE);
	}
	return 0;
}
