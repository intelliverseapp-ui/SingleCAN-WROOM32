#pragma once

#include "esp_err.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------
// Initialize Bluetooth Classic and SPP
// ------------------------------------------------------------
esp_err_t bt_spp_init(void);

// ------------------------------------------------------------
// Return nonzero when an SPP client is connected
// ------------------------------------------------------------
int bt_spp_is_connected(void);

// ------------------------------------------------------------
// Return the active SPP connection handle
//
// Returns 0 when no SPP client is connected.
// ------------------------------------------------------------
uint32_t bt_spp_get_handle(void);

// ------------------------------------------------------------
// Send raw data to the connected SPP client
//
// The supplied string must be null-terminated.
// A newline is appended by the implementation so Android can
// process each JSON response as a complete packet.
//
// Returns:
//   ESP_OK when the write request is accepted
//   ESP_ERR_INVALID_ARG for a null or empty message
//   ESP_ERR_INVALID_STATE when no client is connected
//   Another ESP-IDF error code if the SPP write fails
// ------------------------------------------------------------
esp_err_t bt_spp_send(const char *message);

#ifdef __cplusplus
}
#endif