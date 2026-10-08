#include <stdio.h>

#include "bt_spp.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "singlecan_can.h"
#include "singlecan_leds.h"
#include "tasks.h"

static const char *TAG =
    "SingleCAN_MAIN";

// ------------------------------------------------------------
// TERMINAL FAULT LOOP
// ------------------------------------------------------------

static void enter_fault_state(
    const char *subsystem,
    esp_err_t error
)
{
    ESP_LOGE(
        TAG,
        "%s initialization failed: %s",
        subsystem,
        esp_err_to_name(
            error
        )
    );

    led_set_red();

    while (true) {
        vTaskDelay(
            pdMS_TO_TICKS(
                1000
            )
        );
    }
}

// ------------------------------------------------------------
// NVS INITIALIZATION
// ------------------------------------------------------------

static esp_err_t initialize_nvs(void)
{
    esp_err_t result =
        nvs_flash_init();

    if (
        result ==
            ESP_ERR_NVS_NO_FREE_PAGES ||
        result ==
            ESP_ERR_NVS_NEW_VERSION_FOUND
    ) {
        ESP_LOGW(
            TAG,
            "NVS requires erase and reinitialization"
        );

        result =
            nvs_flash_erase();

        if (
            result !=
            ESP_OK
        ) {
            return result;
        }

        result =
            nvs_flash_init();
    }

    return result;
}

// ------------------------------------------------------------
// APP MAIN
// BLUETOOTH SPP COMMAND BACKEND + TWAI
// ------------------------------------------------------------

void app_main(void)
{
    printf(
        ">>> APP_MAIN ENTERED "
        "(SingleCAN-WROOM32, Bluetooth SPP) <<<\n"
    );

    fflush(
        stdout
    );

    const esp_err_t nvs_result =
        initialize_nvs();

    if (
        nvs_result !=
        ESP_OK
    ) {
        enter_fault_state(
            "NVS",
            nvs_result
        );
    }

    ESP_LOGI(
        TAG,
        "SingleCAN ESP32-WROOM32 starting "
        "(Bluetooth SPP + CAN)"
    );

    /*
     * Initialize the status LED before the other subsystems so
     * startup failures can be indicated consistently.
     */
    singlecan_leds_init();

    led_set_red();

    vTaskDelay(
        pdMS_TO_TICKS(
            200
        )
    );

    /*
     * Initialize TWAI in normal mode.
     *
     * PCAN hardware and PCAN-Explorer 7 perform vehicle CAN capture
     * and decoding. SingleCAN remains the future verified-command
     * transmitter.
     *
     * No guessed Honda CAN mappings are installed, and the current
     * command handlers remain non-transmitting stubs.
     */
    const esp_err_t can_result =
        singlecan_init();

    if (
        can_result !=
        ESP_OK
    ) {
        enter_fault_state(
            "CAN",
            can_result
        );
    }

    /*
     * Start the TWAI health monitor and bus-off recovery task.
     *
     * The receive task currently remains available for TWAI driver
     * health and incoming-frame handling. Raw CAN frames are no
     * longer routed through the obsolete TCP-era telemetry queue.
     */
    start_can_rx_task();

    /*
     * Initialize Bluetooth Classic SPP.
     *
     * bt_spp.c owns command framing, connection sessions, and the
     * single completion-driven outbound response writer.
     */
    const esp_err_t bluetooth_result =
        bt_spp_init();

    if (
        bluetooth_result !=
        ESP_OK
    ) {
        enter_fault_state(
            "Bluetooth SPP",
            bluetooth_result
        );
    }

    led_set_green();

    ESP_LOGI(
        TAG,
        "SingleCAN startup initialized "
        "(Bluetooth SPP command backend + CAN)"
    );

    while (true) {
        vTaskDelay(
            pdMS_TO_TICKS(
                1000
            )
        );
    }
}