#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------
// Initialize Bluetooth Classic + SPP
// ------------------------------------------------------------
esp_err_t bt_spp_init(void);

// ------------------------------------------------------------
// Returns non-zero if an SPP client is connected
// ------------------------------------------------------------
int bt_spp_is_connected(void);

// ------------------------------------------------------------
// Returns current SPP connection handle (0 if none)
// ------------------------------------------------------------
uint32_t bt_spp_get_handle(void);

#ifdef __cplusplus
}
#endif
