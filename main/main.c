#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "singlecan_leds.h"
#include "singlecan_can.h"
#include "tcp_queue.h"

#include "wifi_ap.h"
#include "http_server.h"
#include "tasks.h"

static const char *TAG = "SingleCAN";

// ------------------------------------------------------------
// APP MAIN — CLEAN, MINIMAL, PRODUCTION-GRADE
// ------------------------------------------------------------
void app_main(void)
{
    esp_log_level_set("TCP", ESP_LOG_VERBOSE);

    printf(">>> APP_MAIN ENTERED (SingleCAN-WROOM32) <<<\n");
    fflush(stdout);

    ESP_LOGI(TAG, "SingleCAN ESP32-WROOM32 starting...");

    // LED subsystem
    singlecan_leds_init();
    led_set_red();
    vTaskDelay(pdMS_TO_TICKS(200));

    // TCP outbound queue
    tcp_queue_init();

    // CAN initialization
    esp_err_t can_ret = singlecan_init();
    if (can_ret != ESP_OK) {
        ESP_LOGE(TAG, "CAN init FAILED: %s", esp_err_to_name(can_ret));
        led_set_red();
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // CAN RX task
    start_can_rx_task();

    // Wi-Fi STA (connect to phone hotspot, static IP)
    esp_err_t wifi_ret = init_wifi_sta();
    if (wifi_ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi STA init FAILED: %s", esp_err_to_name(wifi_ret));
        led_set_red();
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    led_set_green();

    // HTTP server
    httpd_handle_t http_server = start_http_server();
    if (http_server == NULL) {
        ESP_LOGE(TAG, "HTTP server start FAILED");
        led_set_red();
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // TCP server task
    start_tcp_server_task();

    ESP_LOGI(TAG, "SingleCAN ready (Wi-Fi STA + CAN + TCP Queue + TCP Server + HTTP Server)");

    // Idle loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
