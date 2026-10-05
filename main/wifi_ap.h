#ifndef WIFI_AP_H
#define WIFI_AP_H

#include "esp_err.h"

// Initialize Wi-Fi in STA mode and connect to the phone hotspot.
// Returns ESP_OK on success, or an error code on failure.
esp_err_t init_wifi_sta(void);

#endif // WIFI_AP_H
