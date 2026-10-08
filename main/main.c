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

#define BLUETOOTH_SPP_READY_TIMEOUT_MS 10000

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
     * No guessed Honda CAN mappings are installed. The verified-
     * mapping gate prevents arbitrary identifiers or payloads from
     * reaching the private TWAI transmission function.
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
     * Start the TWAI receive-drain and health-monitor tasks.
     *
     * Raw CAN frames are not logged, serialized, queued, or sent
     * through Bluetooth.
     */
    start_can_rx_task();

    /*
     * Begin Bluetooth Classic SPP initialization.
     *
     * This function starts an asynchronous initialization sequence.
     * It does not by itself prove that the SPP server is ready.
     */
    const esp_err_t bluetooth_init_result =
        bt_spp_init();

    if (
        bluetooth_init_result !=
        ESP_OK
    ) {
        enter_fault_state(
            "Bluetooth SPP initialization",
            bluetooth_init_result
        );
    }

    ESP_LOGI(
        TAG,
        "Waiting for SPP server-start confirmation"
    );

    /*
     * Do not report the command backend as ready until
     * ESP_SPP_START_EVT confirms successful server startup.
     */
    const esp_err_t bluetooth_ready_result =
        bt_spp_wait_until_ready(
            BLUETOOTH_SPP_READY_TIMEOUT_MS
        );

    if (
        bluetooth_ready_result !=
        ESP_OK
    ) {
        enter_fault_state(
            "Bluetooth SPP server startup",
            bluetooth_ready_result
        );
    }

    if (!bt_spp_is_server_ready()) {
        enter_fault_state(
            "Bluetooth SPP readiness verification",
            ESP_ERR_INVALID_STATE
        );
    }

    /*
     * Green now means the required startup path has completed:
     *
     * - NVS initialized
     * - TWAI initialized
     * - TWAI tasks started
     * - Bluetooth controller and Bluedroid initialized
     * - SPP server startup confirmed
     */
    led_set_green();

    ESP_LOGI(
        TAG,
        "SingleCAN ready "
        "(Bluetooth SPP server confirmed + CAN initialized)"
    );

    while (true) {
        vTaskDelay(
            pdMS_TO_TICKS(
                1000
            )
        );
    }
}