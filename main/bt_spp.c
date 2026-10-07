#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "esp_spp_api.h"

#include "bt_spp.h"
#include "singlecan_commands.h"

static const char *TAG = "BT_SPP";
static const char *BT_DEVICE_NAME = "BabyNodeCAN";

#define BT_SPP_TX_BUFFER_SIZE 256
#define BT_SPP_RX_BUFFER_SIZE 256

static uint32_t s_spp_handle = 0;
static int s_spp_connected = 0;
static int s_spp_congested = 0;
static int s_spp_write_in_progress = 0;

static char s_tx_buffer[BT_SPP_TX_BUFFER_SIZE];

// ------------------------------------------------------------
// PUBLIC ACCESSORS
// ------------------------------------------------------------
int bt_spp_is_connected(void)
{
    return s_spp_connected;
}

uint32_t bt_spp_get_handle(void)
{
    return s_spp_handle;
}

// ------------------------------------------------------------
// SEND DATA TO CONNECTED SPP CLIENT
// ------------------------------------------------------------
esp_err_t bt_spp_send(const char *message)
{
    if (message == NULL || message[0] == '\0') {
        ESP_LOGE(TAG, "Cannot send a null or empty SPP message");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_spp_connected || s_spp_handle == 0) {
        ESP_LOGW(TAG, "Cannot send SPP message: no client connected");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_spp_congested) {
        ESP_LOGW(TAG, "Cannot send SPP message: connection is congested");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_spp_write_in_progress) {
        ESP_LOGW(TAG, "Cannot send SPP message: write already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    size_t message_length = strlen(message);

    if (message_length + 2 > sizeof(s_tx_buffer)) {
        ESP_LOGE(
            TAG,
            "SPP message too large: %zu bytes, maximum=%zu",
            message_length,
            sizeof(s_tx_buffer) - 2
        );

        return ESP_ERR_INVALID_SIZE;
    }

    memset(s_tx_buffer, 0, sizeof(s_tx_buffer));

    memcpy(
        s_tx_buffer,
        message,
        message_length
    );

    s_tx_buffer[message_length] = '\n';
    s_tx_buffer[message_length + 1] = '\0';

    const int transmit_length =
        (int)(message_length + 1);

    ESP_LOGI(
        TAG,
        "SPP TX: %s",
        message
    );

    s_spp_write_in_progress = 1;

    esp_err_t result =
        esp_spp_write(
            s_spp_handle,
            transmit_length,
            (uint8_t *)s_tx_buffer
        );

    if (result != ESP_OK) {
        s_spp_write_in_progress = 0;

        ESP_LOGE(
            TAG,
            "esp_spp_write failed: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    return ESP_OK;
}

// ------------------------------------------------------------
// SPP EVENT HANDLER
// ------------------------------------------------------------
static void spp_event_handler(
    esp_spp_cb_event_t event,
    esp_spp_cb_param_t *param
)
{
    switch (event) {

    case ESP_SPP_INIT_EVT: {
        ESP_LOGI(
            TAG,
            "SPP init event, starting SPP server..."
        );

        esp_err_t result =
            esp_bt_gap_set_device_name(
                BT_DEVICE_NAME
            );

        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "esp_bt_gap_set_device_name failed: %s",
                esp_err_to_name(result)
            );
        }

        result =
            esp_bt_gap_set_scan_mode(
                ESP_BT_CONNECTABLE,
                ESP_BT_GENERAL_DISCOVERABLE
            );

        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "esp_bt_gap_set_scan_mode failed: %s",
                esp_err_to_name(result)
            );
        }

        result =
            esp_spp_start_srv(
                ESP_SPP_SEC_AUTHENTICATE,
                ESP_SPP_ROLE_SLAVE,
                0,
                BT_DEVICE_NAME
            );

        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "esp_spp_start_srv failed: %s",
                esp_err_to_name(result)
            );
        }

        break;
    }

    case ESP_SPP_START_EVT:
        ESP_LOGI(
            TAG,
            "SPP server started, handle=%" PRIu32,
            param->start.handle
        );
        break;

    case ESP_SPP_SRV_OPEN_EVT:
        s_spp_handle =
            param->srv_open.handle;

        s_spp_connected = 1;
        s_spp_congested = 0;
        s_spp_write_in_progress = 0;

        ESP_LOGI(
            TAG,
            "SPP client connected, handle=%" PRIu32,
            s_spp_handle
        );
        break;

    case ESP_SPP_CLOSE_EVT:
        ESP_LOGI(
            TAG,
            "SPP connection closed, handle=%" PRIu32,
            param->close.handle
        );

        s_spp_connected = 0;
        s_spp_handle = 0;
        s_spp_congested = 0;
        s_spp_write_in_progress = 0;

        memset(
            s_tx_buffer,
            0,
            sizeof(s_tx_buffer)
        );
        break;

    case ESP_SPP_DATA_IND_EVT: {
        ESP_LOGI(
            TAG,
            "SPP data received, len=%d",
            param->data_ind.len
        );

        if (param->data_ind.len <= 0) {
            ESP_LOGW(
                TAG,
                "SPP data event contained no data"
            );

            break;
        }

        char packet[BT_SPP_RX_BUFFER_SIZE];

        memset(
            packet,
            0,
            sizeof(packet)
        );

        size_t copy_length =
            (size_t)param->data_ind.len;

        if (copy_length > sizeof(packet) - 1) {
            copy_length =
                sizeof(packet) - 1;

            ESP_LOGW(
                TAG,
                "SPP packet truncated to %zu bytes",
                copy_length
            );
        }

        memcpy(
            packet,
            param->data_ind.data,
            copy_length
        );

        packet[copy_length] = '\0';

        ESP_LOGI(
            TAG,
            "SPP RX: %s",
            packet
        );

        singlecan_commands_process(
            packet
        );

        break;
    }

    case ESP_SPP_CONG_EVT:
        s_spp_congested =
            param->cong.cong ? 1 : 0;

        ESP_LOGI(
            TAG,
            "SPP congestion changed, congested=%d",
            s_spp_congested
        );
        break;

    case ESP_SPP_WRITE_EVT:
        s_spp_write_in_progress = 0;

        if (param->write.cong) {
            s_spp_congested = 1;
        }

        ESP_LOGI(
            TAG,
            "SPP write completed, len=%d, congested=%d",
            param->write.len,
            param->write.cong
        );

        if (param->write.status != ESP_SPP_SUCCESS) {
            ESP_LOGE(
                TAG,
                "SPP write failed, status=%d",
                param->write.status
            );
        }

        break;

    default:
        ESP_LOGI(
            TAG,
            "SPP event: %d",
            event
        );
        break;
    }
}

// ------------------------------------------------------------
// BLUETOOTH CLASSIC AND SPP INITIALIZATION
// ESP-IDF 5.3.x
// ------------------------------------------------------------
esp_err_t bt_spp_init(void)
{
    ESP_LOGI(
        TAG,
        "Initializing Bluetooth controller (Classic)..."
    );

    esp_err_t result =
        esp_bt_mem_release(
            ESP_BT_MODE_BLE
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to release BLE memory: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    esp_bt_controller_config_t bt_config =
        BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    result =
        esp_bt_controller_init(
            &bt_config
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Bluetooth controller init failed: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        esp_bt_controller_enable(
            ESP_BT_MODE_CLASSIC_BT
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Bluetooth controller enable failed: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "Initializing Bluedroid..."
    );

    result =
        esp_bluedroid_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Bluedroid init failed: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        esp_bluedroid_enable();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Bluedroid enable failed: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "Registering SPP callback..."
    );

    result =
        esp_spp_register_callback(
            spp_event_handler
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "SPP callback registration failed: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    esp_spp_cfg_t spp_config = {
        .mode = ESP_SPP_MODE_CB,
        .enable_l2cap_ertm = true,
        .tx_buffer_size = 0
    };

    ESP_LOGI(
        TAG,
        "Initializing SPP (enhanced)..."
    );

    result =
        esp_spp_enhanced_init(
            &spp_config
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "SPP enhanced init failed: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "Bluetooth SPP initialization complete."
    );

    return ESP_OK;
}