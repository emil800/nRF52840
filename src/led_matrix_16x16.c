#include "led_matrix_16x16.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>

/* --- LED Matrix Configuration --- */
#define STRIP_NODE DT_ALIAS(led_strip)
#define WIDTH  16
#define HEIGHT 16
#define NUM_PIXELS (WIDTH * HEIGHT)

#define FWD_R 32
#define FWD_G 32
#define FWD_B 32
#define AMBER_R 64
#define AMBER_G 32
#define AMBER_B 0

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[NUM_PIXELS];

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

void led_matrix_16x16_draw_forward(uint32_t frame)
{
	memset(pixels, 0, sizeof(pixels));
	int origin_y = -(int)(frame % HEIGHT);

	blit_up_arrow(origin_y, FWD_R, FWD_G, FWD_B);
}

void led_matrix_16x16_draw_left(uint32_t frame)
{
	memset(pixels, 0, sizeof(pixels));
	int origin_x = -(int)(frame % WIDTH);

	blit_left_arrow(origin_x, AMBER_R, AMBER_G, AMBER_B);
}

void led_matrix_16x16_draw_right(uint32_t frame)
{
	memset(pixels, 0, sizeof(pixels));
	int origin_x = (int)(frame % WIDTH);

	blit_right_arrow(origin_x, AMBER_R, AMBER_G, AMBER_B);
}

int led_matrix_16x16_init(void)
{
	if (!device_is_ready(strip)) {
		return -1;
	}
	return 0;
}

void led_matrix_16x16_clear(void)
{
	memset(pixels, 0, sizeof(pixels));
}

int led_matrix_16x16_show(void)
{
	return led_strip_update_rgb(strip, pixels, NUM_PIXELS);
}

int led_matrix_16x16_clear_and_show(void)
{
	led_matrix_16x16_clear();
	return led_matrix_16x16_show();
}

