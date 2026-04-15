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
#define LED_MATRIX_16X16_DRAW_CIRCLES

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

#ifndef LED_MATRIX_16X16_DRAW_CIRCLES
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
#endif /* !LED_MATRIX_16X16_DRAW_CIRCLES */

#ifndef LED_MATRIX_16X16_DRAW_CIRCLES
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
#endif /* !LED_MATRIX_16X16_DRAW_CIRCLES */

#ifdef LED_MATRIX_16X16_DRAW_CIRCLES
static void blit_filled_circle(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b)
{
	const int r2 = radius * radius;

	for (int y = 0; y < HEIGHT; y++) {
		for (int x = 0; x < WIDTH; x++) {
			const int dx = x - cx;
			const int dy = y - cy;
			if ((dx * dx + dy * dy) <= r2) {
				plot(x, y, r, g, b);
			}
		}
	}
}

static void blit_half_ring(int cx, int cy, int inner_radius, int outer_radius, bool right_half,
			   uint8_t r, uint8_t g, uint8_t b)
{
	const int inner2 = inner_radius * inner_radius;
	const int outer2 = outer_radius * outer_radius;

	for (int y = 0; y < HEIGHT; y++) {
		for (int x = 0; x < WIDTH; x++) {
			if (right_half) {
				if (x < cx) {
					continue;
				}
			} else {
				if (x > cx) {
					continue;
				}
			}

			const int dx = x - cx;
			const int dy = y - cy;
			const int d2 = (dx * dx + dy * dy);
			if (d2 <= outer2 && d2 >= inner2) {
				plot(x, y, r, g, b);
			}
		}
	}
}
#endif /* LED_MATRIX_16X16_DRAW_CIRCLES */

void led_matrix_16x16_draw_forward(uint32_t frame)
{
#ifdef LED_MATRIX_16X16_DRAW_CIRCLES
	ARG_UNUSED(frame);
	memset(pixels, 0, sizeof(pixels));

	/* Center at (7,7) gives a nice symmetric circle on 16x16. */
	blit_filled_circle(7, 7, 3, FWD_R, 0, 0);
#else
	memset(pixels, 0, sizeof(pixels));
	int origin_y = -(int)(frame % HEIGHT);

	blit_up_arrow(origin_y, FWD_R, FWD_G, FWD_B);
#endif
}

void led_matrix_16x16_draw_left(uint32_t frame)
{
#ifdef LED_MATRIX_16X16_DRAW_CIRCLES
	memset(pixels, 0, sizeof(pixels));

	/* Base: constant red filled circle in the middle (radius 3). */
	blit_filled_circle(7, 7, 3, FWD_R, 0, 0);

	/* Overlay: blinking amber half ring around the red circle (left side). */
	const bool blink_on = ((frame / 4U) % 2U) == 0U;
	if (blink_on) {
		blit_half_ring(7, 7, 4, 7, false, AMBER_R, AMBER_G, AMBER_B);
	}
#else
	memset(pixels, 0, sizeof(pixels));
	int origin_x = -(int)(frame % WIDTH);

	blit_left_arrow(origin_x, AMBER_R, AMBER_G, AMBER_B);
#endif
}

void led_matrix_16x16_draw_right(uint32_t frame)
{
#ifdef LED_MATRIX_16X16_DRAW_CIRCLES
	memset(pixels, 0, sizeof(pixels));

	/* Base: constant red filled circle in the middle (radius 3). */
	blit_filled_circle(7, 7, 3, FWD_R, 0, 0);

	/* Overlay: blinking amber half ring around the red circle (right side). */
	const bool blink_on = ((frame / 4U) % 2U) == 0U;
	if (blink_on) {
		blit_half_ring(7, 7, 4, 7, true, AMBER_R, AMBER_G, AMBER_B);
	}
#else
	memset(pixels, 0, sizeof(pixels));
	int origin_x = (int)(frame % WIDTH);

	blit_right_arrow(origin_x, AMBER_R, AMBER_G, AMBER_B);
#endif
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

