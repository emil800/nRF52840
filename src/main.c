#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/led_strip.h>
#include <hal/nrf_i2s.h>
#include "battery_monitor.h"
#include "ble.h"

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

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[NUM_PIXELS];

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
	err = ble_init();
	if (err) {
		return -1;
	}

	/* Initialize battery monitor */
	err = battery_monitor_init();
	if (err) {
		return -1;
	}
	(void)k_work_schedule(&battery_check_work, K_SECONDS(5));

	while (1) {
		if (!ble_is_active()) {
			memset(pixels, 0, sizeof(pixels));
			(void)led_strip_update_rgb(strip, pixels, NUM_PIXELS);
			frame = 0;
			ble_wait_for_active();
			continue;
		}

		switch (ble_get_mode()) {
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
