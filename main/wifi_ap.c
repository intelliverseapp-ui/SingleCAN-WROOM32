#include "wifi_ap.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"

static const char *TAG_WIFI = "WIFI_AP";

// ------------------------------------------------------------
// Initialize Wi-Fi Access Point
// ------------------------------------------------------------
esp_err_t init_wifi_ap(void)
{
    ESP_LOGI(TAG_WIFI, "Initializing Wi-Fi AP...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize network stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif = esp_netif_create_default_wifi_ap();
    (void)netif;

    // Initialize Wi-Fi driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configure AP
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "SingleCAN-WROOM32",
            .ssid_len = 0,
            .channel = 1,
            .password = "singlecan123",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .pmf_cfg = { .required = false },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_WIFI, "Wi-Fi AP started");
    ESP_LOGI(TAG_WIFI, "SSID: SingleCAN-WROOM32  PASSWORD: singlecan123");
    ESP_LOGI(TAG_WIFI, "Connect from Android and IP will be 192.168.4.1");

    return ESP_OK;
}
