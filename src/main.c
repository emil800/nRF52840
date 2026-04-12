#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

/* --- LED Matrix Configuration --- */
#define STRIP_NODE DT_ALIAS(led_strip)
#define WIDTH  16
#define HEIGHT 16
#define NUM_PIXELS (WIDTH * HEIGHT)

enum bike_state { STATE_FORWARD = 0, STATE_LEFT = 1, STATE_RIGHT = 2 };
static volatile enum bike_state current_mode = STATE_FORWARD;

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[NUM_PIXELS];

/* --- Bluetooth Service Logic --- */

// Custom Service UUID: 12345678-1234-5678-1234-567812345678
static struct bt_uuid_128 bike_svc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x567812345678));

// Characteristic UUID: 12345678-1234-5678-1234-567812345679
static struct bt_uuid_128 bike_char_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x567812345679));

// Callback when phone writes data to the Bluetooth characteristic
static ssize_t write_bike_mode(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	uint8_t *value = (uint8_t *)buf;
	if (len > 0) {
		if (value[0] == 0) current_mode = STATE_FORWARD;
		else if (value[0] == 1) current_mode = STATE_LEFT;
		else if (value[0] == 2) current_mode = STATE_RIGHT;
	}
	return len;
}

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

static uint32_t get_idx(int x, int y) {
    // Handling Zig-Zag wiring common in 16x16 matrices
    if (y % 2 != 0) {
        return (y * WIDTH) + (WIDTH - 1 - x);
    }
    return (y * WIDTH) + x;
}

void draw_forward(uint32_t frame) {
    memset(pixels, 0, sizeof(pixels));
    int y_anim = HEIGHT - 1 - (frame % HEIGHT);
    for (int x = 6; x < 10; x++) {
        pixels[get_idx(x, y_anim)].g = 2; // Dim White/Green
        pixels[get_idx(x, y_anim)].r = 2;
        pixels[get_idx(x, y_anim)].b = 2;
    }
}

void draw_left(uint32_t frame) {
    memset(pixels, 0, sizeof(pixels));
    int progress = (frame % 8) * 2;
    for (int x = WIDTH-1; x > (WIDTH-1 - progress); x--) {
        for (int y = 6; y < 10; y++) {
            pixels[get_idx(x/2, y)].r = 16; // Amber
            pixels[get_idx(x/2, y)].g = 8;
        }
    }
}

void draw_right(uint32_t frame) {
    memset(pixels, 0, sizeof(pixels));
    int progress = (frame % 8) * 2;
    for (int x = 0; x < progress; x++) {
        for (int y = 6; y < 10; y++) {
            pixels[get_idx(8 + x/2, y)].r = 16; // Amber
            pixels[get_idx(8 + x/2, y)].g = 8;
        }
    }
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

	while (1) {
		switch (current_mode) {
			case STATE_LEFT:    draw_left(frame);    break;
			case STATE_RIGHT:   draw_right(frame);   break;
			default:            draw_forward(frame); break;
		}

		led_strip_update_rgb(strip, pixels, NUM_PIXELS);
		frame++;
		k_msleep(100);
	}
	return 0;
}
