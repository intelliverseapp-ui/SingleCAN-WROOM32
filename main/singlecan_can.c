#include "singlecan_can.h"

#include "esp_log.h"
#include "singlecan_leds.h"
#include "tasks.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static const char *TAG =
    "SingleCAN";

#define SINGLECAN_RECEIVE_TIMEOUT_MS 10
#define SINGLECAN_TRANSMIT_TIMEOUT_MS 10
#define SINGLECAN_STANDARD_ID_MAX 0x7FFU
#define SINGLECAN_CLASSIC_CAN_MAX_DLC 8U

/*
 * This flag controls whether the TWAI subsystem is available.
 *
 * Enabling TWAI does not bypass the verified-mapping transmission
 * gate. No arbitrary CAN identifier or payload transmission API is
 * exposed by this module.
 */
bool g_can_enabled =
    true;

// ------------------------------------------------------------
// PRIVATE VERIFIED-FRAME DEFINITION
// ------------------------------------------------------------

typedef struct {
    uint32_t identifier;

    uint8_t data_length_code;

    uint8_t data[
        SINGLECAN_CLASSIC_CAN_MAX_DLC
    ];
} singlecan_verified_frame_t;

// ------------------------------------------------------------
// PRIVATE VERIFIED-MAPPING LOOKUP
// ------------------------------------------------------------

static esp_err_t lookup_verified_frame(
    singlecan_verified_command_t command,
    singlecan_verified_frame_t *frame
)
{
    if (frame == NULL) {
        ESP_LOGE(
            TAG,
            "Verified-frame lookup rejected: null output"
        );

        return ESP_ERR_INVALID_ARG;
    }

    /*
     * No Honda Accord CAN mappings have been verified yet.
     *
     * Future mappings must be added as explicit enum cases. Each
     * case must supply a reviewed identifier, DLC, and payload that
     * were captured through PCAN hardware and PCAN-Explorer 7,
     * independently verified, and approved for the Phase 1 scope.
     *
     * Generic runtime identifiers and payloads must never be
     * accepted by this function.
     */
    switch (command) {
        case SINGLECAN_VERIFIED_COMMAND_NONE:
            return ESP_ERR_NOT_SUPPORTED;

        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
}

// ------------------------------------------------------------
// PRIVATE VERIFIED-FRAME VALIDATION
// ------------------------------------------------------------

static esp_err_t validate_verified_frame(
    const singlecan_verified_frame_t *frame
)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * The current verified-frame structure represents standard
     * 11-bit Classic CAN data frames only.
     */
    if (
        frame->identifier >
        SINGLECAN_STANDARD_ID_MAX
    ) {
        ESP_LOGE(
            TAG,
            "Verified frame rejected: invalid standard identifier"
        );

        return ESP_ERR_INVALID_ARG;
    }

    if (
        frame->data_length_code >
        SINGLECAN_CLASSIC_CAN_MAX_DLC
    ) {
        ESP_LOGE(
            TAG,
            "Verified frame rejected: invalid DLC"
        );

        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

// ------------------------------------------------------------
// PRIVATE VERIFIED-FRAME TRANSMISSION
// ------------------------------------------------------------

static esp_err_t transmit_verified_frame(
    const singlecan_verified_frame_t *frame
)
{
    const esp_err_t validation_result =
        validate_verified_frame(
            frame
        );

    if (
        validation_result !=
        ESP_OK
    ) {
        return validation_result;
    }

    twai_message_t message = {
        .identifier =
            frame->identifier,
        .data_length_code =
            frame->data_length_code,
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

    for (
        uint8_t index = 0;
        index <
        frame->data_length_code;
        ++index
    ) {
        message.data[index] =
            frame->data[index];
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
            "Verified CAN transmission failed: %s",
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
        "Verified CAN mapping submitted successfully"
    );

    return ESP_OK;
}

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
// CAN STATUS
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
            "CAN_STATUS: %s MODE: NORMAL "
            "TX_POLICY: VERIFIED_MAPPINGS_ONLY\n",
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
// PUBLIC VERIFIED-MAPPING TRANSMISSION GATE
// ------------------------------------------------------------

esp_err_t singlecan_send_verified(
    singlecan_verified_command_t command
)
{
    if (!g_can_enabled) {
        ESP_LOGW(
            TAG,
            "Verified CAN transmission rejected: CAN is disabled"
        );

        return ESP_ERR_INVALID_STATE;
    }

    if (
        !singlecan_tasks_is_twai_running()
    ) {
        ESP_LOGW(
            TAG,
            "Verified CAN transmission rejected: "
            "TWAI supervision is not RUNNING"
        );

        return ESP_ERR_INVALID_STATE;
    }

    singlecan_verified_frame_t frame = {
        .identifier =
            0,
        .data_length_code =
            0,
        .data = {
            0
        }
    };

    const esp_err_t lookup_result =
        lookup_verified_frame(
            command,
            &frame
        );

    if (
        lookup_result !=
        ESP_OK
    ) {
        ESP_LOGW(
            TAG,
            "CAN transmission rejected: "
            "no verified mapping is installed"
        );

        /*
         * No verified mapping was found. twai_transmit() is not
         * called anywhere along this rejection path.
         */
        return lookup_result;
    }

    if (
        !singlecan_tasks_is_twai_running()
    ) {
        ESP_LOGW(
            TAG,
            "Verified CAN transmission canceled: "
            "TWAI left the RUNNING state"
        );

        return ESP_ERR_INVALID_STATE;
    }

    return transmit_verified_frame(
        &frame
    );
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
            "Discarding received frame with invalid DLC"
        );

        singlecan_leds_error();

        return ESP_ERR_INVALID_SIZE;
    }

    singlecan_leds_can_rx_active();

    /*
     * PCAN hardware and PCAN-Explorer 7 own CAN capture and decoding.
     *
     * SingleCAN removes this frame from the TWAI receive queue only
     * to maintain controller health. The raw frame is not logged,
     * serialized, queued, or forwarded through Bluetooth.
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