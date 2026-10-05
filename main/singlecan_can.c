#include "singlecan_can.h"
#include "singlecan_leds.h"
#include "tcp_queue.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SingleCAN";

// CAN enabled flag — MUST be global (not static) so http_server.c can link it
bool g_can_enabled = true;

// ------------------------------------------------------------
// Initialize CAN (TWAI) driver
// ------------------------------------------------------------
esp_err_t singlecan_init(void)
{
    ESP_LOGI(TAG, "Initializing SingleCAN (TWAI) driver...");

    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT(SINGLECAN_TX_PIN, SINGLECAN_RX_PIN, TWAI_MODE_NORMAL);

    twai_timing_config_t t_config = SINGLECAN_BITRATE;
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI install failed: %s", esp_err_to_name(err));
        singlecan_leds_error();
        return err;
    }

    err = twai_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI start failed: %s", esp_err_to_name(err));
        singlecan_leds_error();
        return err;
    }

    ESP_LOGI(TAG, "SingleCAN initialized successfully.");
    return ESP_OK;
}

// ------------------------------------------------------------
// CAN Enable / Disable
// ------------------------------------------------------------
esp_err_t singlecan_enable_can(void)
{
    g_can_enabled = true;
    ESP_LOGI(TAG, "CAN ENABLED");
    return ESP_OK;
}

esp_err_t singlecan_disable_can(void)
{
    g_can_enabled = false;
    ESP_LOGI(TAG, "CAN DISABLED");
    return ESP_OK;
}

// ------------------------------------------------------------
// CAN Status String
// ------------------------------------------------------------
void singlecan_get_status(char *out, size_t out_len)
{
    snprintf(out, out_len,
             "CAN_STATUS: %s\n",
             g_can_enabled ? "ENABLED" : "DISABLED");
}

// ------------------------------------------------------------
// Transmit CAN frame — NOW WITH POINTER + LENGTH VALIDATION
// ------------------------------------------------------------
esp_err_t singlecan_send(uint32_t can_id, uint8_t *data, uint8_t len)
{
    if (!g_can_enabled) {
        ESP_LOGW(TAG, "CAN TX ignored — CAN disabled");
        return ESP_ERR_INVALID_STATE;
    }

    if (len > 8) {
        ESP_LOGE(TAG, "CAN frame length cannot exceed 8 bytes.");
        return ESP_ERR_INVALID_ARG;
    }

    if (len > 0 && data == NULL) {
        ESP_LOGE(TAG, "singlecan_send called with NULL data pointer");
        return ESP_ERR_INVALID_ARG;
    }

    twai_message_t msg = {
        .identifier = can_id,
        .data_length_code = len,
        .rtr = 0,
        .ss = 0,
        .extd = 0
    };

    if (len > 0) {
        memcpy(msg.data, data, len);
    }

    esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(10));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CAN transmit failed: %s", esp_err_to_name(err));
        return err;
    }

    singlecan_leds_can_tx_active();
    return ESP_OK;
}

// ------------------------------------------------------------
// Receive CAN frame (non-blocking)
// ------------------------------------------------------------
esp_err_t singlecan_receive(twai_message_t *msg)
{
    if (!g_can_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = twai_receive(msg, pdMS_TO_TICKS(10));

    if (err == ESP_ERR_TIMEOUT) {
        return ESP_ERR_TIMEOUT;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CAN receive error: %s", esp_err_to_name(err));
        return err;
    }

    singlecan_leds_can_rx_active();

    char line[256];
    snprintf(
        line,
        sizeof(line),
        "CAN_RX %lu %u %02X %02X %02X %02X %02X %02X %02X %02X",
        (unsigned long)msg->identifier,
        msg->data_length_code,
        msg->data[0], msg->data[1], msg->data[2], msg->data[3],
        msg->data[4], msg->data[5], msg->data[6], msg->data[7]
    );

    tcp_queue_push(line);

    return ESP_OK;
}

// ------------------------------------------------------------
// Stop CAN driver
// ------------------------------------------------------------
esp_err_t singlecan_stop(void)
{
    ESP_LOGI(TAG, "Stopping SingleCAN driver...");

    esp_err_t err = twai_stop();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI stop failed: %s", esp_err_to_name(err));
        return err;
    }

    err = twai_driver_uninstall();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI uninstall failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "SingleCAN stopped.");
    return ESP_OK;
}
