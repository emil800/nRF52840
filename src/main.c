#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define STRIP_NODE DT_ALIAS(led_strip)
#define NUM_PIXELS DT_PROP(DT_ALIAS(led_strip), chain_length)
#define DELAY_MS   30
#define BRIGHTNESS 10

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[NUM_PIXELS];

static struct led_rgb hsv_to_rgb(uint16_t hue, uint8_t sat, uint8_t val)
{
	uint8_t region = hue / 60;
	uint16_t remainder = (hue % 60) * 255 / 60;

	uint8_t p = (uint16_t)val * (255 - sat) / 255;
	uint8_t q = (uint16_t)val * (255 - (sat * remainder / 255)) / 255;
	uint8_t t = (uint16_t)val * (255 - (sat * (255 - remainder) / 255)) / 255;

	struct led_rgb rgb;

	switch (region) {
	case 0:  rgb = (struct led_rgb){.r = val, .g = t,   .b = p};   break;
	case 1:  rgb = (struct led_rgb){.r = q,   .g = val, .b = p};   break;
	case 2:  rgb = (struct led_rgb){.r = p,   .g = val, .b = t};   break;
	case 3:  rgb = (struct led_rgb){.r = p,   .g = q,   .b = val}; break;
	case 4:  rgb = (struct led_rgb){.r = t,   .g = p,   .b = val}; break;
	default: rgb = (struct led_rgb){.r = val, .g = p,   .b = q};   break;
	}

	return rgb;
}

int main(void)
{
	if (!device_is_ready(strip)) {
		LOG_ERR("LED strip device %s is not ready", strip->name);
		return -ENODEV;
	}

	LOG_INF("Driving %d WS2812B pixels on %s", NUM_PIXELS, strip->name);

	size_t pos = 0;
	uint16_t hue = 0;

	while (1) {
		memset(pixels, 0, sizeof(pixels));
		pixels[pos] = hsv_to_rgb(hue, 255, BRIGHTNESS);

		int rc = led_strip_update_rgb(strip, pixels, NUM_PIXELS);
		if (rc) {
			LOG_ERR("led_strip_update_rgb failed: %d", rc);
		}

		pos++;
		if (pos >= NUM_PIXELS) {
			pos = 0;
			hue = (hue + 30) % 360;
		}
		k_msleep(DELAY_MS);
	}

	return 0;
}
