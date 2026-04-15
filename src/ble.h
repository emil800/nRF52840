#pragma once

#include <stdbool.h>

enum bike_state { STATE_FORWARD = 0, STATE_LEFT = 1, STATE_RIGHT = 2 };

/**
 * Initialize Bluetooth stack, GATT service, and start advertising.
 *
 * Returns 0 on success, negative errno-style value on failure.
 */
int ble_init(void);

/**
 * True when connected and LEDs are enabled by the central.
 */
bool ble_is_active(void);

/**
 * Block until BLE becomes active (connected + LEDs enabled).
 *
 * Safe to call repeatedly from the main loop.
 */
void ble_wait_for_active(void);

/**
 * Current requested bike mode (valid when active, but can be read anytime).
 */
enum bike_state ble_get_mode(void);

