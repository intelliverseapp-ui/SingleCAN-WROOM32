#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "esp_spp_api.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "bt_spp.h"
#include "bt_spp_framer.h"
#include "bt_spp_session.h"
#include "bt_spp_writer.h"
#include "singlecan_commands.h"

static const char *TAG =
    "BT_SPP";

static const char *BT_DEVICE_NAME =
    "BabyNodeCAN";

#define BT_SPP_READINESS_EVENT_READY BIT0
#define BT_SPP_READINESS_EVENT_FAILED BIT1

static EventGroupHandle_t s_readiness_events =
    NULL;

static portMUX_TYPE s_state_lock =
    portMUX_INITIALIZER_UNLOCKED;

static volatile int s_spp_server_ready =
    0;

static volatile esp_err_t s_spp_startup_error =
    ESP_ERR_INVALID_STATE;

// ------------------------------------------------------------
// COMPLETED-FRAME DELIVERY
// ------------------------------------------------------------

static void bt_spp_handle_complete_frame(
    const char *frame
)
{
    if (frame == NULL) {
        ESP_LOGE(
            TAG,
            "SPP framer delivered a null frame"
        );

        return;
    }

    singlecan_commands_process(
        frame
    );
}

// ------------------------------------------------------------
// FORWARD DECLARATIONS
// ------------------------------------------------------------

static void spp_event_handler(
    esp_spp_cb_event_t event,
    esp_spp_cb_param_t *param
);

// ------------------------------------------------------------
// SERVER READINESS
// ------------------------------------------------------------
// ------------------------------------------------------------
// SERVER READINESS
// ------------------------------------------------------------

static void bt_spp_reset_readiness_state(void)
{
    portENTER_CRITICAL(
        &s_state_lock
    );

    s_spp_server_ready =
        0;

    s_spp_startup_error =
        ESP_ERR_INVALID_STATE;

    portEXIT_CRITICAL(
        &s_state_lock
    );

    if (
        s_readiness_events !=
        NULL
    ) {
        xEventGroupClearBits(
            s_readiness_events,
            BT_SPP_READINESS_EVENT_READY |
                BT_SPP_READINESS_EVENT_FAILED
        );
    }
}

static void bt_spp_mark_server_ready(void)
{
    portENTER_CRITICAL(
        &s_state_lock
    );

    s_spp_server_ready =
        1;

    s_spp_startup_error =
        ESP_OK;

    portEXIT_CRITICAL(
        &s_state_lock
    );

    if (
        s_readiness_events !=
        NULL
    ) {
        xEventGroupClearBits(
            s_readiness_events,
            BT_SPP_READINESS_EVENT_FAILED
        );

        xEventGroupSetBits(
            s_readiness_events,
            BT_SPP_READINESS_EVENT_READY
        );
    }

    ESP_LOGI(
        TAG,
        "SPP server readiness confirmed"
    );
}

static void bt_spp_mark_server_failed(
    esp_err_t error
)
{
    portENTER_CRITICAL(
        &s_state_lock
    );

    s_spp_server_ready =
        0;

    s_spp_startup_error =
        error;

    portEXIT_CRITICAL(
        &s_state_lock
    );

    if (
        s_readiness_events !=
        NULL
    ) {
        xEventGroupClearBits(
            s_readiness_events,
            BT_SPP_READINESS_EVENT_READY
        );

        xEventGroupSetBits(
            s_readiness_events,
            BT_SPP_READINESS_EVENT_FAILED
        );
    }

    ESP_LOGE(
        TAG,
        "SPP server startup failed: %s",
        esp_err_to_name(
            error
        )
    );
}

static esp_err_t bt_spp_create_readiness_events(void)
{
    if (
        s_readiness_events !=
        NULL
    ) {
        ESP_LOGW(
            TAG,
            "SPP readiness event group is already initialized"
        );

        return ESP_ERR_INVALID_STATE;
    }

    s_readiness_events =
        xEventGroupCreate();

    if (
        s_readiness_events ==
        NULL
    ) {
        ESP_LOGE(
            TAG,
            "Failed to create SPP readiness event group"
        );

        return ESP_ERR_NO_MEM;
    }

    bt_spp_reset_readiness_state();

    return ESP_OK;
}

// ------------------------------------------------------------
// EXTRACTED SESSION AND WRITER BRIDGE
// ------------------------------------------------------------

static void bt_spp_disconnect_writer_session(
    uint32_t handle,
    uint32_t session_id
)
{
    if (
        !bt_spp_session_matches(
            handle,
            session_id
        )
    ) {
        ESP_LOGW(
            TAG,
            "Ignoring stale writer disconnect request"
        );

        return;
    }

    const esp_err_t disconnect_result =
        esp_spp_disconnect(
            handle
        );

    if (
        disconnect_result !=
        ESP_OK
    ) {
        ESP_LOGE(
            TAG,
            "Failed to disconnect timed-out SPP session: %s",
            esp_err_to_name(
                disconnect_result
            )
        );

        /*
         * The Bluetooth stack could not complete the requested
         * disconnect. Invalidate local ownership so no further work
         * can be submitted to the failed session.
         */
        if (
            bt_spp_session_matches(
                handle,
                session_id
            )
        ) {
            bt_spp_session_force_close();

            singlecan_commands_reset_session();

            bt_spp_framer_reset();

            bt_spp_writer_on_disconnected();
        }
    }
}

static void bt_spp_complete_active_session_close(
    uint32_t handle
)
{
    if (
        !bt_spp_session_close(
            handle
        )
    ) {
        return;
    }

    singlecan_commands_reset_session();

    bt_spp_framer_reset();

    bt_spp_writer_on_disconnected();
}

// ------------------------------------------------------------
// PUBLIC READINESS AND CONNECTION ACCESSORS
// ------------------------------------------------------------
// ------------------------------------------------------------
// PUBLIC READINESS AND CONNECTION ACCESSORS
// ------------------------------------------------------------

int bt_spp_is_server_ready(void)
{
    int ready;

    portENTER_CRITICAL(
        &s_state_lock
    );

    ready =
        s_spp_server_ready;

    portEXIT_CRITICAL(
        &s_state_lock
    );

    return ready;
}

esp_err_t bt_spp_wait_until_ready(
    uint32_t timeout_ms
)
{
    if (
        s_readiness_events ==
        NULL
    ) {
        ESP_LOGE(
            TAG,
            "Cannot wait for SPP readiness before initialization"
        );

        return ESP_ERR_INVALID_STATE;
    }

    if (timeout_ms == 0) {
        return bt_spp_is_server_ready()
            ? ESP_OK
            : ESP_ERR_TIMEOUT;
    }

    const EventBits_t result_bits =
        xEventGroupWaitBits(
            s_readiness_events,
            BT_SPP_READINESS_EVENT_READY |
                BT_SPP_READINESS_EVENT_FAILED,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(
                timeout_ms
            )
        );

    if (
        result_bits &
        BT_SPP_READINESS_EVENT_READY
    ) {
        return ESP_OK;
    }

    if (
        result_bits &
        BT_SPP_READINESS_EVENT_FAILED
    ) {
        esp_err_t startup_error;

        portENTER_CRITICAL(
            &s_state_lock
        );

        startup_error =
            s_spp_startup_error;

        portEXIT_CRITICAL(
            &s_state_lock
        );

        return startup_error !=
                ESP_OK
            ? startup_error
            : ESP_ERR_INVALID_STATE;
    }

    ESP_LOGE(
        TAG,
        "Timed out waiting for SPP server readiness"
    );

    return ESP_ERR_TIMEOUT;
}

int bt_spp_is_connected(void)
{
    return bt_spp_session_is_connected();
}

uint32_t bt_spp_get_handle(void)
{
    return bt_spp_session_get_handle();
}

// ------------------------------------------------------------
// QUEUE DATA FOR THE EXTRACTED SPP WRITER
// ------------------------------------------------------------

esp_err_t bt_spp_send(
    const char *message
)
{
    if (
        !bt_spp_is_server_ready() ||
        !bt_spp_session_is_connected() ||
        bt_spp_session_get_handle() == 0
    ) {
        ESP_LOGW(
            TAG,
            "Cannot queue SPP message: no active session"
        );

        return ESP_ERR_INVALID_STATE;
    }

    return bt_spp_writer_send(
        message,
        bt_spp_session_get_id()
    );
}

// ------------------------------------------------------------
// SPP EVENT HANDLER
// ------------------------------------------------------------

static void spp_event_handler(
    esp_spp_cb_event_t event,
    esp_spp_cb_param_t *param
)
{
    if (param == NULL) {
        ESP_LOGE(
            TAG,
            "SPP callback received null parameters"
        );

        return;
    }

    switch (event) {
    case ESP_SPP_INIT_EVT: {
        if (
            param->init.status !=
            ESP_SPP_SUCCESS
        ) {
            ESP_LOGE(
                TAG,
                "SPP initialization event failed, status=%d",
                param->init.status
            );

            bt_spp_mark_server_failed(
                ESP_FAIL
            );

            break;
        }

        ESP_LOGI(
            TAG,
            "SPP init event, starting SPP server"
        );

        esp_err_t result =
            esp_bt_gap_set_device_name(
                BT_DEVICE_NAME
            );

        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Setting Bluetooth device name failed: %s",
                esp_err_to_name(
                    result
                )
            );

            bt_spp_mark_server_failed(
                result
            );

            break;
        }

        result =
            esp_bt_gap_set_scan_mode(
                ESP_BT_CONNECTABLE,
                ESP_BT_GENERAL_DISCOVERABLE
            );

        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Setting Bluetooth scan mode failed: %s",
                esp_err_to_name(
                    result
                )
            );

            bt_spp_mark_server_failed(
                result
            );

            break;
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
                "Starting SPP server failed: %s",
                esp_err_to_name(
                    result
                )
            );

            bt_spp_mark_server_failed(
                result
            );
        }

        break;
    }

    case ESP_SPP_START_EVT:
        if (
            param->start.status ==
            ESP_SPP_SUCCESS
        ) {
            ESP_LOGI(
                TAG,
                "SPP server started, handle=%" PRIu32,
                param->start.handle
            );

            bt_spp_mark_server_ready();
        } else {
            ESP_LOGE(
                TAG,
                "SPP server start event failed, status=%d",
                param->start.status
            );

            bt_spp_mark_server_failed(
                ESP_FAIL
            );
        }

        break;

    case ESP_SPP_SRV_OPEN_EVT: {
        const uint32_t new_handle =
            param->srv_open.handle;

        uint32_t accepted_session_id =
            0;

        const int connection_is_valid =
            bt_spp_is_server_ready() &&
            param->srv_open.status ==
                ESP_SPP_SUCCESS;

        const int connection_accepted =
            connection_is_valid &&
            bt_spp_session_accept(
                new_handle,
                &accepted_session_id
            );

        if (!connection_accepted) {
            ESP_LOGW(
                TAG,
                "Rejecting additional or invalid SPP client, "
                "handle=%" PRIu32,
                new_handle
            );

            const esp_err_t disconnect_result =
                esp_spp_disconnect(
                    new_handle
                );

            if (
                disconnect_result !=
                ESP_OK
            ) {
                ESP_LOGE(
                    TAG,
                    "Failed to disconnect rejected SPP client: %s",
                    esp_err_to_name(
                        disconnect_result
                    )
                );
            }

            break;
        }

        singlecan_commands_reset_session();

        bt_spp_framer_reset();

        bt_spp_writer_on_connected();

        ESP_LOGI(
            TAG,
            "SPP client connected, handle=%" PRIu32
            ", session=%" PRIu32,
            new_handle,
            accepted_session_id
        );

        break;
    }

    case ESP_SPP_CLOSE_EVT: {
        const uint32_t closed_handle =
            param->close.handle;

        if (
            !bt_spp_session_handle_is_active(
                closed_handle
            )
        ) {
            ESP_LOGI(
                TAG,
                "Rejected or stale SPP client closed, "
                "handle=%" PRIu32,
                closed_handle
            );

            break;
        }

        ESP_LOGI(
            TAG,
            "Active SPP connection closed, handle=%" PRIu32,
            closed_handle
        );

        bt_spp_complete_active_session_close(
            closed_handle
        );

        break;
    }

    case ESP_SPP_DATA_IND_EVT: {
        const uint32_t active_handle =
            bt_spp_session_get_handle();

        const int active_session =
            bt_spp_is_server_ready() &&
            bt_spp_session_is_connected() &&
            active_handle != 0;

        if (
            !active_session ||
            param->data_ind.handle !=
                active_handle ||
            param->data_ind.status !=
                ESP_SPP_SUCCESS ||
            param->data_ind.len <= 0 ||
            param->data_ind.data == NULL
        ) {
            ESP_LOGW(
                TAG,
                "Ignoring invalid or stale SPP data event"
            );

            break;
        }

        ESP_LOGI(
            TAG,
            "SPP data received, len=%d",
            param->data_ind.len
        );

        bt_spp_framer_process(
            param->data_ind.data,
            (size_t)param->data_ind.len
        );

        break;
    }

    case ESP_SPP_CONG_EVT: {
        const int event_matches_active_handle =
            bt_spp_session_set_congested(
                param->cong.handle,
                param->cong.cong
                    ? 1
                    : 0
            );

        if (!event_matches_active_handle) {
            ESP_LOGW(
                TAG,
                "Ignoring stale congestion event"
            );

            break;
        }

        ESP_LOGI(
            TAG,
            "SPP congestion changed, congested=%d",
            param->cong.cong
                ? 1
                : 0
        );

        bt_spp_writer_on_congestion_changed();

        break;
    }

    case ESP_SPP_WRITE_EVT:
        if (
            !bt_spp_writer_on_write_event(
                param
            )
        ) {
            ESP_LOGW(
                TAG,
                "SPP write event did not match active writer state"
            );
        }

        break;

    default:
        ESP_LOGD(
            TAG,
            "SPP event: %d",
            event
        );

        break;
    }
}

// ------------------------------------------------------------
// BLUETOOTH INITIALIZATION
// ------------------------------------------------------------

esp_err_t bt_spp_init(void)
{
    if (
        !bt_spp_framer_init(
            bt_spp_handle_complete_frame
        )
    ) {
        ESP_LOGE(
            TAG,
            "Failed to initialize SPP receive framer"
        );

        return ESP_ERR_INVALID_STATE;
    }

    singlecan_commands_reset_session();

    bt_spp_session_init();

    bt_spp_reset_readiness_state();

    const esp_err_t readiness_result =
        bt_spp_create_readiness_events();

    if (readiness_result != ESP_OK) {
        return readiness_result;
    }

    const esp_err_t writer_result =
        bt_spp_writer_init(
            bt_spp_session_get_writer_snapshot,
            bt_spp_disconnect_writer_session
        );

    if (
        writer_result !=
        ESP_OK
    ) {
        bt_spp_mark_server_failed(
            writer_result
        );

        return writer_result;
    }

    ESP_LOGI(
        TAG,
        "Initializing Bluetooth controller (Classic)"
    );

    esp_err_t result =
        esp_bt_mem_release(
            ESP_BT_MODE_BLE
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to release BLE memory: %s",
            esp_err_to_name(
                result
            )
        );

        bt_spp_mark_server_failed(
            result
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
            "Bluetooth controller initialization failed: %s",
            esp_err_to_name(
                result
            )
        );

        bt_spp_mark_server_failed(
            result
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
            esp_err_to_name(
                result
            )
        );

        bt_spp_mark_server_failed(
            result
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "Initializing Bluedroid"
    );

    result =
        esp_bluedroid_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Bluedroid initialization failed: %s",
            esp_err_to_name(
                result
            )
        );

        bt_spp_mark_server_failed(
            result
        );

        return result;
    }

    result =
        esp_bluedroid_enable();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Bluedroid enable failed: %s",
            esp_err_to_name(
                result
            )
        );

        bt_spp_mark_server_failed(
            result
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "Registering SPP callback"
    );

    result =
        esp_spp_register_callback(
            spp_event_handler
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "SPP callback registration failed: %s",
            esp_err_to_name(
                result
            )
        );

        bt_spp_mark_server_failed(
            result
        );

        return result;
    }

    esp_spp_cfg_t spp_config = {
        .mode =
            ESP_SPP_MODE_CB,
        .enable_l2cap_ertm =
            true,
        .tx_buffer_size =
            0
    };

    ESP_LOGI(
        TAG,
        "Initializing SPP enhanced mode"
    );

    result =
        esp_spp_enhanced_init(
            &spp_config
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "SPP enhanced initialization failed: %s",
            esp_err_to_name(
                result
            )
        );

        bt_spp_mark_server_failed(
            result
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "Bluetooth SPP initialization requested; "
        "awaiting server-start confirmation"
    );

    return ESP_OK;
}