#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdint.h>

/**
 * @brief Initialize battery monitor (ADC + enable pin)
 * @return 0 on success, negative errno on failure
 */
int battery_monitor_init(void);

/**
 * @brief Read battery voltage
 * @return voltage in millivolts, or -1 on error
 */
int battery_monitor_read_mv(void);

/**
 * @brief Update onboard LEDs based on voltage
 * @param mv voltage in millivolts from battery_monitor_read_mv()
 */
void battery_monitor_update_leds(int mv);

#endif /* BATTERY_MONITOR_H */