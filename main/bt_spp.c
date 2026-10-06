#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_err.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"

#include "bt_spp.h"

// Bring in your command processor
extern void singlecan_commands_process(const char *cmd);

static const char *TAG = "BT_SPP";
static const char *BT_DEVICE_NAME = "BabyNodeCAN";

static uint32_t s_spp_handle = 0;
static int s_spp_connected = 0;

// ------------------------------------------------------------
// Public accessors
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
// SPP EVENT HANDLER
// ------------------------------------------------------------
static void spp_event_handler(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event) {

    case ESP_SPP_INIT_EVT: {
        ESP_LOGI(TAG, "SPP init event, starting SPP server...");

        esp_err_t ret = esp_bt_gap_set_device_name(BT_DEVICE_NAME);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_bt_gap_set_device_name failed: %s", esp_err_to_name(ret));
        }

        ret = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_bt_gap_set_scan_mode failed: %s", esp_err_to_name(ret));
        }

        ret = esp_spp_start_srv(ESP_SPP_SEC_AUTHENTICATE,
                                ESP_SPP_ROLE_SLAVE,
                                0,
                                BT_DEVICE_NAME);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_spp_start_srv failed: %s", esp_err_to_name(ret));
        }
        break;
    }

    case ESP_SPP_START_EVT:
        ESP_LOGI(TAG, "SPP server started, handle=%" PRIu32, param->start.handle);
        break;

    case ESP_SPP_SRV_OPEN_EVT:
        ESP_LOGI(TAG, "SPP client connected, handle=%" PRIu32, param->srv_open.handle);
        s_spp_handle = param->srv_open.handle;
        s_spp_connected = 1;
        break;

    case ESP_SPP_CLOSE_EVT:
        ESP_LOGI(TAG, "SPP connection closed, handle=%" PRIu32, param->close.handle);
        s_spp_connected = 0;
        s_spp_handle = 0;
        break;

    case ESP_SPP_DATA_IND_EVT:
        ESP_LOGI(TAG, "SPP data received, len=%d", param->data_ind.len);

        if (param->data_ind.len > 0) {
            char cmd[128];
            memset(cmd, 0, sizeof(cmd));

            size_t copy_len = param->data_ind.len;
            if (copy_len > sizeof(cmd) - 1) {
                copy_len = sizeof(cmd) - 1;
            }

            memcpy(cmd, param->data_ind.data, copy_len);

            ESP_LOGI(TAG, "SPP CMD: %s", cmd);

            singlecan_commands_process(cmd);
        }
        break;

    case ESP_SPP_CONG_EVT:
        ESP_LOGI(TAG, "SPP congestion event, cong=%d", param->cong.cong);
        break;

    case ESP_SPP_WRITE_EVT:
        ESP_LOGI(TAG, "SPP write event, len=%d, cong=%d",
                 param->write.len, param->write.cong);
        break;

    default:
        ESP_LOGI(TAG, "SPP event: %d", event);
        break;
    }
}

// ------------------------------------------------------------
// BLUETOOTH CLASSIC + SPP INITIALIZATION (ESP-IDF v5.3.x)
// ------------------------------------------------------------
esp_err_t bt_spp_init(void)
{
    ESP_LOGI(TAG, "Initializing Bluetooth controller (Classic)...");

    // Release BLE memory (we only use Classic BT)
    esp_err_t ret = esp_bt_mem_release(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to release BLE memory: %s", esp_err_to_name(ret));
        return ret;
    }

    // Use IDF-provided default config so it matches sdkconfig
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Initializing Bluedroid...");
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Registering SPP callback...");
    ret = esp_spp_register_callback(spp_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPP register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_spp_cfg_t spp_cfg = {
        .mode = ESP_SPP_MODE_CB,
        .enable_l2cap_ertm = true,
        .tx_buffer_size = 0,
    };

    ESP_LOGI(TAG, "Initializing SPP (enhanced)...");
    ret = esp_spp_enhanced_init(&spp_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPP enhanced init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Bluetooth SPP initialization complete.");
    return ESP_OK;
}
