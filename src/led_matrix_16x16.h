#pragma once

#include <stdint.h>

#define LED_MATRIX_16X16_FRAME_DELAY_MS 100

/**
 * Initialize the 16x16 LED matrix (WS2812 strip via Zephyr led_strip).
 *
 * Returns 0 on success, negative errno-style value on failure.
 */
int led_matrix_16x16_init(void);

/**
 * Render a forward arrow frame into the internal pixel buffer.
 */
void led_matrix_16x16_draw_forward(uint32_t frame);

/**
 * Render a left arrow frame into the internal pixel buffer.
 */
void led_matrix_16x16_draw_left(uint32_t frame);

/**
 * Render a right arrow frame into the internal pixel buffer.
 */
void led_matrix_16x16_draw_right(uint32_t frame);

/**
 * Clear internal buffer (does not transmit).
 */
void led_matrix_16x16_clear(void);

/**
 * Transmit the current buffer to the LEDs.
 *
 * Returns 0 on success, negative errno-style value on failure.
 */
int led_matrix_16x16_show(void);

/**
 * Convenience: clear buffer and immediately transmit.
 */
int led_matrix_16x16_clear_and_show(void);

