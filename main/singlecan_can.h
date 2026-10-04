#pragma once

#include "driver/twai.h"
#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

// ------------------------------------------------------------
// GPIO pin assignments for ESP32‑WROOM32 DevKit V1
// ------------------------------------------------------------
#define SINGLECAN_TX_PIN  GPIO_NUM_5
#define SINGLECAN_RX_PIN  GPIO_NUM_4

// CAN timing: 500 kbps
#define SINGLECAN_BITRATE  TWAI_TIMING_CONFIG_500KBITS()

// Logging tag
static const char *TAG_SINGLECAN = "SingleCAN";

// ------------------------------------------------------------
// Initialization / Shutdown
// ------------------------------------------------------------
esp_err_t singlecan_init(void);
esp_err_t singlecan_stop(void);

// ------------------------------------------------------------
// CAN Enable / Disable
// ------------------------------------------------------------
esp_err_t singlecan_enable_can(void);
esp_err_t singlecan_disable_can(void);

// ------------------------------------------------------------
// CAN Status
// ------------------------------------------------------------
void singlecan_get_status(char *out, size_t out_len);

// ------------------------------------------------------------
// CAN Transmit / Receive
// ------------------------------------------------------------
esp_err_t singlecan_send(uint32_t can_id, uint8_t *data, uint8_t len);
esp_err_t singlecan_receive(twai_message_t *msg);
