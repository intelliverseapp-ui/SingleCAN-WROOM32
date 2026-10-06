#pragma once

#include <stdint.h>
#include <stdbool.h>

// ------------------------------------------------------------
// Initialization
// ------------------------------------------------------------
void singlecan_leds_init(void);

// ------------------------------------------------------------
// Basic LED control (GPIO2 onboard LED)
// ------------------------------------------------------------
void led_set_red(void);
void led_set_green(void);
void led_set_off(void);

// ------------------------------------------------------------
// Automotive Status API (mapped to simple LED behavior)
// ------------------------------------------------------------

// CAN bus status
void singlecan_leds_can_idle(void);
void singlecan_leds_can_rx_active(void);
void singlecan_leds_can_tx_active(void);

// Bluetooth SPP status
void singlecan_leds_bt_disconnected(void);
void singlecan_leds_bt_connected(void);

// Error indicator
void singlecan_leds_error(void);

