#ifndef WIFI_AP_H
#define WIFI_AP_H

#include "esp_err.h"

// Initialize the Wi-Fi Access Point.
// Returns ESP_OK on success, or an error code on failure.
esp_err_t init_wifi_ap(void);

#endif // WIFI_AP_H
