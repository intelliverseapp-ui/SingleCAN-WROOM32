#include "singlecan_can.h"

#include "esp_log.h"
#include "singlecan_leds.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG =
    "SingleCAN";

#define SINGLECAN_RECEIVE_TIMEOUT_MS 10
#define SINGLECAN_TRANSMIT_TIMEOUT_MS 10
#define SINGLECAN_STANDARD_ID_MAX 0x7FFU
#define SINGLECAN_CLASSIC_CAN_MAX_DLC 8U

/*
 * This flag controls whether the TWAI subsystem is available.
 *
 * The flag does not bypass frame validation. Future command handlers
 * must use verified, allowlisted Honda frame definitions before they
 * call singlecan_send().
 */
bool g_can_enabled =
    true;

// ------------------------------------------------------------
// INITIALIZE TWAI DRIVER
// ------------------------------------------------------------

esp_err_t singlecan_init(void)
{
    ESP_LOGI(
        TAG,
        "Initializing SingleCAN TWAI driver in normal mode"
    );

    twai_general_config_t general_config =
        TWAI_GENERAL_CONFIG_DEFAULT(
            SINGLECAN_TX_PIN,
            SINGLECAN_RX_PIN,
            TWAI_MODE_NORMAL
        );

    twai_timing_config_t timing_config =
        SINGLECAN_BITRATE;

    twai_filter_config_t filter_config =
        TWAI_FILTER_CONFIG_ACCEPT_ALL();

    const esp_err_t install_result =
        twai_driver_install(
            &general_config,
            &timing_config,
            &filter_config
        );

    if (
        install_result !=
        ESP_OK
    ) {
        ESP_LOGE(
            TAG,
            "TWAI driver installation failed: %s",
            esp_err_to_name(
                install_result
            )
        );

        singlecan_leds_error();

        return install_result;
    }

    const esp_err_t start_result =
        twai_start();

    if (
        start_result !=
        ESP_OK
    ) {
        ESP_LOGE(
            TAG,
            "TWAI driver startup failed: %s",
            esp_err_to_name(
                start_result
            )
        );

        const esp_err_t uninstall_result =
            twai_driver_uninstall();

        if (
            uninstall_result !=
            ESP_OK
        ) {
            ESP_LOGE(
                TAG,
                "TWAI cleanup after startup failure failed: %s",
                esp_err_to_name(
                    uninstall_result
                )
            );
        }

        singlecan_leds_error();

        return start_result;
    }

    g_can_enabled =
        true;

    ESP_LOGI(
        TAG,
        "SingleCAN TWAI driver initialized successfully"
    );

    return ESP_OK;
}

// ------------------------------------------------------------
// ENABLE OR DISABLE TWAI OPERATIONS
// ------------------------------------------------------------

esp_err_t singlecan_enable_can(void)
{
    g_can_enabled =
        true;

    ESP_LOGI(
        TAG,
        "CAN operations enabled"
    );

    return ESP_OK;
}

esp_err_t singlecan_disable_can(void)
{
    g_can_enabled =
        false;

    ESP_LOGI(
        TAG,
        "CAN operations disabled"
    );

    return ESP_OK;
}

// ------------------------------------------------------------
// CAN STATUS STRING
// ------------------------------------------------------------

void singlecan_get_status(
    char *output,
    size_t output_length
)
{
    if (
        output == NULL ||
        output_length == 0
    ) {
        return;
    }

    const int written =
        snprintf(
            output,
            output_length,
            "CAN_STATUS: %s MODE: NORMAL\n",
            g_can_enabled
                ? "ENABLED"
                : "DISABLED"
        );

    if (written < 0) {
        output[0] =
            '\0';
    }
}

// ------------------------------------------------------------
// TRANSMIT ONE STANDARD CLASSIC-CAN FRAME
// ------------------------------------------------------------

esp_err_t singlecan_send(
    uint32_t can_id,
    uint8_t *data,
    uint8_t length
)
{
    if (!g_can_enabled) {
        ESP_LOGW(
            TAG,
            "CAN transmission rejected because CAN is disabled"
        );

        return ESP_ERR_INVALID_STATE;
    }

    /*
     * SingleCAN currently constructs standard-format frames only.
     * Reject identifiers that do not fit the 11-bit standard range.
     */
    if (
        can_id >
        SINGLECAN_STANDARD_ID_MAX
    ) {
        ESP_LOGE(
            TAG,
            "CAN transmission rejected: invalid standard identifier"
        );

        return ESP_ERR_INVALID_ARG;
    }

    if (
        length >
        SINGLECAN_CLASSIC_CAN_MAX_DLC
    ) {
        ESP_LOGE(
            TAG,
            "CAN transmission rejected: DLC exceeds 8"
        );

        return ESP_ERR_INVALID_ARG;
    }

    if (
        length > 0 &&
        data == NULL
    ) {
        ESP_LOGE(
            TAG,
            "CAN transmission rejected: null payload"
        );

        return ESP_ERR_INVALID_ARG;
    }

    /*
     * This API remains available for future verified mappings.
     *
     * No current canonical-command stub calls this function.
     * Do not connect generic client input directly to this API.
     * Future handlers must select reviewed identifiers, DLCs, and
     * payloads from an explicit Phase 1 mapping allowlist.
     */
    twai_message_t message = {
        .identifier =
            can_id,
        .data_length_code =
            length,
        .rtr =
            0,
        .ss =
            0,
        .self =
            0,
        .dlc_non_comp =
            0,
        .extd =
            0
    };

    if (length > 0) {
        memcpy(
            message.data,
            data,
            length
        );
    }

    const esp_err_t transmit_result =
        twai_transmit(
            &message,
            pdMS_TO_TICKS(
                SINGLECAN_TRANSMIT_TIMEOUT_MS
            )
        );

    if (
        transmit_result !=
        ESP_OK
    ) {
        ESP_LOGE(
            TAG,
            "CAN transmission failed: %s",
            esp_err_to_name(
                transmit_result
            )
        );

        singlecan_leds_error();

        return transmit_result;
    }

    singlecan_leds_can_tx_active();

    ESP_LOGI(
        TAG,
        "Verified CAN frame submitted: id=0x%03lX dlc=%u",
        (unsigned long)can_id,
        length
    );

    return ESP_OK;
}

// ------------------------------------------------------------
// RECEIVE ONE CAN FRAME
// ------------------------------------------------------------

esp_err_t singlecan_receive(
    twai_message_t *message
)
{
    if (message == NULL) {
        ESP_LOGE(
            TAG,
            "CAN receive rejected: null message pointer"
        );

        return ESP_ERR_INVALID_ARG;
    }

    if (!g_can_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t receive_result =
        twai_receive(
            message,
            pdMS_TO_TICKS(
                SINGLECAN_RECEIVE_TIMEOUT_MS
            )
        );

    if (
        receive_result ==
        ESP_ERR_TIMEOUT
    ) {
        return ESP_ERR_TIMEOUT;
    }

    if (
        receive_result !=
        ESP_OK
    ) {
        ESP_LOGE(
            TAG,
            "CAN receive failed: %s",
            esp_err_to_name(
                receive_result
            )
        );

        return receive_result;
    }

    if (
        message->data_length_code >
        SINGLECAN_CLASSIC_CAN_MAX_DLC
    ) {
        ESP_LOGE(
            TAG,
            "Discarding received frame with invalid DLC: %u",
            message->data_length_code
        );

        singlecan_leds_error();

        return ESP_ERR_INVALID_SIZE;
    }

    singlecan_leds_can_rx_active();

    /*
     * PCAN hardware and PCAN-Explorer 7 own CAN capture and decoding.
     *
     * SingleCAN consumes the received frame only so the TWAI driver
     * remains drained and its health can be monitored. The raw frame
     * is not logged, serialized, queued, or sent over Bluetooth.
     */
    return ESP_OK;
}

// ------------------------------------------------------------
// STOP TWAI DRIVER
// ------------------------------------------------------------

esp_err_t singlecan_stop(void)
{
    ESP_LOGI(
        TAG,
        "Stopping SingleCAN TWAI driver"
    );

    g_can_enabled =
        false;

    const esp_err_t stop_result =
        twai_stop();

    if (
        stop_result !=
        ESP_OK
    ) {
        ESP_LOGE(
            TAG,
            "TWAI stop failed: %s",
            esp_err_to_name(
                stop_result
            )
        );

        return stop_result;
    }

    const esp_err_t uninstall_result =
        twai_driver_uninstall();

    if (
        uninstall_result !=
        ESP_OK
    ) {
        ESP_LOGE(
            TAG,
            "TWAI uninstall failed: %s",
            esp_err_to_name(
                uninstall_result
            )
        );

        return uninstall_result;
    }

    ESP_LOGI(
        TAG,
        "SingleCAN TWAI driver stopped"
    );

    return ESP_OK;
}