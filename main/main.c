#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "singlecan_leds.h"
#include "singlecan_can.h"
#include "tcp_queue.h"
#include "tasks.h"
#include "bt_spp.h"   // NEW: use your dedicated Bluetooth module

#include "nvs_flash.h"   // NEW: required for NVS initialization

static const char *TAG = "SingleCAN_MAIN";

// ------------------------------------------------------------
// APP MAIN — BLUETOOTH SPP + CAN, NO WI-FI
// ------------------------------------------------------------
void app_main(void)
{
    printf(">>> APP_MAIN ENTERED (SingleCAN-WROOM32, Bluetooth SPP) <<<\n");
    fflush(stdout);

    // ------------------------------------------------------------
    // NVS INITIALIZATION (required for BT RF calibration + config)
    // ------------------------------------------------------------
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    ESP_LOGI(TAG, "SingleCAN ESP32-WROOM32 starting (Bluetooth SPP + CAN)...");

    // LED subsystem
    singlecan_leds_init();
    led_set_red();
    vTaskDelay(pdMS_TO_TICKS(200));

    // Outbound queue (Bluetooth telemetry)
    tcp_queue_init();

    // CAN initialization
    esp_err_t can_ret = singlecan_init();
    if (can_ret != ESP_OK) {
        ESP_LOGE(TAG, "CAN init FAILED: %s", esp_err_to_name(can_ret));
        led_set_red();
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // CAN RX task
    start_can_rx_task();

    // Bluetooth Classic + SPP (handled by bt_spp.c)
    esp_err_t bt_ret = bt_spp_init();
    if (bt_ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth SPP init FAILED: %s", esp_err_to_name(bt_ret));
        led_set_red();
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    led_set_green();

    ESP_LOGI(TAG, "SingleCAN ready (Bluetooth SPP + CAN + outbound queue)");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
