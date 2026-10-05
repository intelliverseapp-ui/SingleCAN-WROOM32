#include "wifi_ap.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"

static const char *TAG_WIFI = "WIFI_STA";

// ------------------------------------------------------------
// Initialize Wi-Fi in STA mode (connect to phone hotspot)
// STATIC IP: 10.84.212.50
// ------------------------------------------------------------
esp_err_t init_wifi_sta(void)
{
    ESP_LOGI(TAG_WIFI, "Initializing Wi-Fi STA (hotspot client) with STATIC IP...");

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

    // Create default Wi-Fi STA interface
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    if (netif == NULL) {
        ESP_LOGE(TAG_WIFI, "Failed to create default Wi-Fi STA netif");
        return ESP_FAIL;
    }

    // STOP DHCP so static IP can be applied
    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));

    // Apply STATIC IP configuration
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr      = esp_ip4addr_aton("10.84.212.50");
    ip_info.netmask.addr = esp_ip4addr_aton("255.255.255.0");
    ip_info.gw.addr      = esp_ip4addr_aton("10.84.212.32");

    ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));

    ESP_LOGI(TAG_WIFI, "STATIC IP CONFIGURED:");
    ESP_LOGI(TAG_WIFI, "  IP:      10.84.212.50");
    ESP_LOGI(TAG_WIFI, "  Netmask: 255.255.255.0");
    ESP_LOGI(TAG_WIFI, "  Gateway: 10.84.212.32");

    // Initialize Wi-Fi driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configure STA to connect to phone hotspot
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "MichaelZFold7",
            .password = "12345678",
        },
    };

    ESP_LOGI(TAG_WIFI, "Configuring STA for SSID: %s", (char *)wifi_config.sta.ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_WIFI, "Wi-Fi STA started, attempting to connect to hotspot...");

    // Start connection
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG_WIFI, "Wi-Fi STA connect requested. STATIC IP active.");

    return ESP_OK;
}
