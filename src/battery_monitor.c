#include "battery_monitor.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/dt-bindings/adc/nrf-saadc.h>

/* --- Voltage thresholds --- */
#define BATTERY_LOW_MV      3400
#define BATTERY_DEAD_MV     3200

/* --- ADC config --- */
#define ADC_RESOLUTION      12
#define ADC_VREF_MV         600
#define ADC_GAIN_FACTOR     6
#define R_TOP_KOHM          1000   /* R16 = 1M  */
#define R_BOT_KOHM          510    /* R17 = 510k */

/* --- Pins --- */
#define READ_BAT_PIN        14     /* P0.14 — sink low to enable divider */

static const struct device *adc_dev  = DEVICE_DT_GET(DT_NODELABEL(adc));
static const struct device *gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define LED2_NODE DT_ALIAS(led2)

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(LED2_NODE, gpios);

static int16_t adc_buf;
static struct adc_sequence adc_seq = {
    .buffer      = &adc_buf,
    .buffer_size = sizeof(adc_buf),
    .resolution  = ADC_RESOLUTION,
};

/* --- Internal helpers --- */

static void read_enable(bool enable)
{
    if (enable) {
        gpio_pin_configure(gpio_dev, READ_BAT_PIN, GPIO_OUTPUT_LOW);
    } else {
        gpio_pin_configure(gpio_dev, READ_BAT_PIN, GPIO_DISCONNECTED);
    }
}

/* --- Public API --- */

int battery_monitor_init(void)
{
    if (!device_is_ready(adc_dev)) {
        return -ENODEV;
    }

    if (!device_is_ready(gpio_dev)) {
        return -ENODEV;
    }

    /* Configure LEDs */
    gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);

    /* Start with divider disabled */
    read_enable(false);

    /* Setup ADC channel */
    struct adc_channel_cfg ch_cfg = {
        .gain             = ADC_GAIN_1_6,
        .reference        = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME_DEFAULT,
        .channel_id       = 7,
        .input_positive   = NRF_SAADC_AIN7,  /* P0.31 */
    };

    int err = adc_channel_setup(adc_dev, &ch_cfg);
    if (err) {
        return err;
    }

    adc_seq.channels = BIT(7);

    return 0;
}

int battery_monitor_read_mv(void)
{
    read_enable(true);
    k_msleep(5);  /* let divider settle */

    int err = adc_read(adc_dev, &adc_seq);
    read_enable(false);

    if (err) {
        return err;
    }

    int32_t mv = adc_buf;
    adc_raw_to_millivolts(ADC_VREF_MV, ADC_GAIN_1_6, ADC_RESOLUTION, &mv);

    /* Scale up through divider */
    int voltage = (int)(mv * (R_TOP_KOHM + R_BOT_KOHM) / R_BOT_KOHM);

    return voltage;
}

void battery_monitor_update_leds(int mv)
{
    if (mv < 0) {
        return;
    }

    if (mv < BATTERY_DEAD_MV) {
        gpio_pin_set_dt(&led0, 1);
        gpio_pin_set_dt(&led1, 1);
        gpio_pin_set_dt(&led2, 1);
    } else if (mv < BATTERY_LOW_MV) {
        gpio_pin_set_dt(&led0, 1);
        gpio_pin_set_dt(&led1, 0);
        gpio_pin_set_dt(&led2, 0);
    } else {
        gpio_pin_set_dt(&led0, 0);
        gpio_pin_set_dt(&led1, 0);
        gpio_pin_set_dt(&led2, 0);
    }
}