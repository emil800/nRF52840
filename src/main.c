#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <hal/nrf_i2s.h>
#include "battery_monitor.h"
#include "ble.h"
#include "led_matrix_16x16.h"

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

	/* Init LED matrix */
	err = led_matrix_16x16_init();
	if (err) {
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
			(void)led_matrix_16x16_clear_and_show();
			frame = 0;
			ble_wait_for_active();
			continue;
		}

		switch (ble_get_mode()) {
		case STATE_LEFT:
			led_matrix_16x16_draw_left(frame);
			break;
		case STATE_RIGHT:
			led_matrix_16x16_draw_right(frame);
			break;
		case STATE_FORWARD:
		default:
			led_matrix_16x16_draw_forward(frame);
			break;
		}
		(void)led_matrix_16x16_show();
		frame++;
		k_msleep(LED_MATRIX_16X16_FRAME_DELAY_MS);
	}
	return 0;
}
