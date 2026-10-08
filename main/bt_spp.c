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
#include "freertos/queue.h"
#include "freertos/task.h"

#include "bt_spp.h"
#include "bt_spp_framer.h"
#include "singlecan_commands.h"

static const char *TAG =
    "BT_SPP";

static const char *BT_DEVICE_NAME =
    "BabyNodeCAN";

#define BT_SPP_TX_BUFFER_SIZE 256
#define BT_SPP_TX_QUEUE_LENGTH 32

#define BT_SPP_WRITER_STACK_SIZE 4096
#define BT_SPP_WRITER_PRIORITY 6

#define BT_SPP_WRITE_TIMEOUT_MS 5000

#define BT_SPP_WRITER_EVENT_CONNECTED BIT0
#define BT_SPP_WRITER_EVENT_WRITABLE BIT1
#define BT_SPP_WRITER_EVENT_WRITE_COMPLETE BIT2
#define BT_SPP_WRITER_EVENT_WRITE_FAILED BIT3

#define BT_SPP_READINESS_EVENT_READY BIT0
#define BT_SPP_READINESS_EVENT_FAILED BIT1

typedef struct {
    char message[
        BT_SPP_TX_BUFFER_SIZE
    ];

    size_t length;

    uint32_t session_id;
} bt_spp_outbound_item_t;

static QueueHandle_t s_outbound_queue =
    NULL;

static EventGroupHandle_t s_writer_events =
    NULL;

static EventGroupHandle_t s_readiness_events =
    NULL;

static TaskHandle_t s_writer_task_handle =
    NULL;

static portMUX_TYPE s_state_lock =
    portMUX_INITIALIZER_UNLOCKED;

static volatile uint32_t s_spp_handle =
    0;

static volatile uint32_t s_session_id =
    0;

static volatile int s_spp_connected =
    0;

static volatile int s_spp_congested =
    0;

static volatile int s_spp_server_ready =
    0;

static volatile esp_err_t s_spp_startup_error =
    ESP_ERR_INVALID_STATE;

static volatile int s_write_in_progress =
    0;

static volatile uint32_t s_inflight_handle =
    0;

static volatile uint32_t s_inflight_session_id =
    0;

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

static void bt_spp_writer_task(
    void *task_argument
);

static void spp_event_handler(
    esp_spp_cb_event_t event,
    esp_spp_cb_param_t *param
);

// ------------------------------------------------------------
// SESSION GENERATION
// ------------------------------------------------------------

static uint32_t bt_spp_next_session_id(void)
{
    uint32_t next_session_id;

    portENTER_CRITICAL(
        &s_state_lock
    );

    s_session_id +=
        1;

    if (s_session_id == 0) {
        s_session_id =
            1;
    }

    next_session_id =
        s_session_id;

    portEXIT_CRITICAL(
        &s_state_lock
    );

    return next_session_id;
}

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
// WRITER-STATE HELPERS
// ------------------------------------------------------------

static void bt_spp_reset_outbound_queue(void)
{
    if (
        s_outbound_queue !=
        NULL
    ) {
        xQueueReset(
            s_outbound_queue
        );
    }
}

static void bt_spp_signal_write_failure(void)
{
    if (
        s_writer_events !=
        NULL
    ) {
        xEventGroupSetBits(
            s_writer_events,
            BT_SPP_WRITER_EVENT_WRITE_FAILED
        );
    }
}

static void bt_spp_update_writer_availability(void)
{
    uint32_t active_handle;
    int connected;
    int congested;
    int write_in_progress;
    int server_ready;

    portENTER_CRITICAL(
        &s_state_lock
    );

    active_handle =
        s_spp_handle;

    connected =
        s_spp_connected;

    congested =
        s_spp_congested;

    write_in_progress =
        s_write_in_progress;

    server_ready =
        s_spp_server_ready;

    portEXIT_CRITICAL(
        &s_state_lock
    );

    if (
        s_writer_events ==
        NULL
    ) {
        return;
    }

    if (
        server_ready &&
        connected &&
        active_handle != 0 &&
        !congested &&
        !write_in_progress
    ) {
        xEventGroupSetBits(
            s_writer_events,
            BT_SPP_WRITER_EVENT_CONNECTED |
                BT_SPP_WRITER_EVENT_WRITABLE
        );
    } else {
        xEventGroupClearBits(
            s_writer_events,
            BT_SPP_WRITER_EVENT_WRITABLE
        );

        if (!connected) {
            xEventGroupClearBits(
                s_writer_events,
                BT_SPP_WRITER_EVENT_CONNECTED
            );
        }
    }
}

static void bt_spp_clear_inflight_write(
    uint32_t expected_handle,
    uint32_t expected_session_id
)
{
    portENTER_CRITICAL(
        &s_state_lock
    );

    if (
        s_write_in_progress &&
        s_inflight_handle ==
            expected_handle &&
        s_inflight_session_id ==
            expected_session_id
    ) {
        s_write_in_progress =
            0;

        s_inflight_handle =
            0;

        s_inflight_session_id =
            0;
    }

    portEXIT_CRITICAL(
        &s_state_lock
    );
}

static void bt_spp_cancel_active_session(void)
{
    portENTER_CRITICAL(
        &s_state_lock
    );

    s_spp_connected =
        0;

    s_spp_handle =
        0;

    s_spp_congested =
        0;

    portEXIT_CRITICAL(
        &s_state_lock
    );

    bt_spp_next_session_id();

    singlecan_commands_reset_session();

    bt_spp_framer_reset();

    bt_spp_reset_outbound_queue();

    if (
        s_writer_events !=
        NULL
    ) {
        xEventGroupClearBits(
            s_writer_events,
            BT_SPP_WRITER_EVENT_CONNECTED |
                BT_SPP_WRITER_EVENT_WRITABLE
        );
    }

    /*
     * Do not clear this failure signal during a new connection.
     * A writer waiting on the old session must consume it.
     */
    bt_spp_signal_write_failure();
}

// ------------------------------------------------------------
// SINGLE COMPLETION-DRIVEN SPP WRITER
// ------------------------------------------------------------

static void bt_spp_writer_task(
    void *task_argument
)
{
    (void)task_argument;

    bt_spp_outbound_item_t item;

    memset(
        &item,
        0,
        sizeof(item)
    );

    ESP_LOGI(
        TAG,
        "Completion-driven SPP writer task started"
    );

    while (true) {
        if (
            xQueueReceive(
                s_outbound_queue,
                &item,
                portMAX_DELAY
            ) !=
            pdTRUE
        ) {
            ESP_LOGE(
                TAG,
                "SPP writer failed to receive queued message"
            );

            continue;
        }

        xEventGroupWaitBits(
            s_writer_events,
            BT_SPP_WRITER_EVENT_CONNECTED |
                BT_SPP_WRITER_EVENT_WRITABLE,
            pdFALSE,
            pdTRUE,
            portMAX_DELAY
        );

        uint32_t write_handle;
        uint32_t write_session_id;
        int connection_valid;

        portENTER_CRITICAL(
            &s_state_lock
        );

        write_handle =
            s_spp_handle;

        write_session_id =
            s_session_id;

        connection_valid =
            s_spp_server_ready &&
            s_spp_connected &&
            write_handle != 0 &&
            !s_spp_congested &&
            !s_write_in_progress &&
            item.session_id ==
                write_session_id;

        if (connection_valid) {
            s_write_in_progress =
                1;

            s_inflight_handle =
                write_handle;

            s_inflight_session_id =
                write_session_id;
        }

        portEXIT_CRITICAL(
            &s_state_lock
        );

        if (!connection_valid) {
            ESP_LOGW(
                TAG,
                "Queued message discarded because session changed"
            );

            memset(
                &item,
                0,
                sizeof(item)
            );

            bt_spp_update_writer_availability();

            continue;
        }

        /*
         * Clear completion state only after ownership of this
         * exact write has been recorded.
         */
        xEventGroupClearBits(
            s_writer_events,
            BT_SPP_WRITER_EVENT_WRITABLE |
                BT_SPP_WRITER_EVENT_WRITE_COMPLETE |
                BT_SPP_WRITER_EVENT_WRITE_FAILED
        );

        ESP_LOGI(
            TAG,
            "Submitting serialized SPP write, len=%zu, "
            "session=%" PRIu32,
            item.length,
            write_session_id
        );

        const esp_err_t write_result =
            esp_spp_write(
                write_handle,
                (int)item.length,
                (uint8_t *)item.message
            );

        if (
            write_result !=
            ESP_OK
        ) {
            ESP_LOGE(
                TAG,
                "esp_spp_write rejected request: %s",
                esp_err_to_name(
                    write_result
                )
            );

            bt_spp_clear_inflight_write(
                write_handle,
                write_session_id
            );

            bt_spp_update_writer_availability();

            memset(
                &item,
                0,
                sizeof(item)
            );

            continue;
        }

        const EventBits_t completion_bits =
            xEventGroupWaitBits(
                s_writer_events,
                BT_SPP_WRITER_EVENT_WRITE_COMPLETE |
                    BT_SPP_WRITER_EVENT_WRITE_FAILED,
                pdTRUE,
                pdFALSE,
                pdMS_TO_TICKS(
                    BT_SPP_WRITE_TIMEOUT_MS
                )
            );

        if (
            completion_bits &
            BT_SPP_WRITER_EVENT_WRITE_COMPLETE
        ) {
            ESP_LOGI(
                TAG,
                "Serialized SPP write completed"
            );
        } else if (
            completion_bits &
            BT_SPP_WRITER_EVENT_WRITE_FAILED
        ) {
            ESP_LOGE(
                TAG,
                "Serialized SPP write failed or session closed"
            );
        } else {
            ESP_LOGE(
                TAG,
                "Serialized SPP write timed out"
            );

            uint32_t active_handle;
            uint32_t active_session_id;
            int same_active_session;

            portENTER_CRITICAL(
                &s_state_lock
            );

            active_handle =
                s_spp_handle;

            active_session_id =
                s_session_id;

            same_active_session =
                s_spp_connected &&
                active_handle ==
                    write_handle &&
                active_session_id ==
                    write_session_id;

            portEXIT_CRITICAL(
                &s_state_lock
            );

            if (same_active_session) {
                const esp_err_t disconnect_result =
                    esp_spp_disconnect(
                        write_handle
                    );

                if (
                    disconnect_result !=
                    ESP_OK
                ) {
                    ESP_LOGE(
                        TAG,
                        "Failed to disconnect timed-out "
                        "SPP session: %s",
                        esp_err_to_name(
                            disconnect_result
                        )
                    );

                    bt_spp_cancel_active_session();
                }
            }
        }

        bt_spp_clear_inflight_write(
            write_handle,
            write_session_id
        );

        bt_spp_update_writer_availability();

        memset(
            &item,
            0,
            sizeof(item)
        );
    }
}

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
    int connected;

    portENTER_CRITICAL(
        &s_state_lock
    );

    connected =
        s_spp_connected;

    portEXIT_CRITICAL(
        &s_state_lock
    );

    return connected;
}

uint32_t bt_spp_get_handle(void)
{
    uint32_t handle;

    portENTER_CRITICAL(
        &s_state_lock
    );

    handle =
        s_spp_handle;

    portEXIT_CRITICAL(
        &s_state_lock
    );

    return handle;
}

// ------------------------------------------------------------
// QUEUE DATA FOR THE SINGLE SPP WRITER
// ------------------------------------------------------------

esp_err_t bt_spp_send(
    const char *message
)
{
    if (
        message == NULL ||
        message[0] == '\0'
    ) {
        ESP_LOGE(
            TAG,
            "Cannot queue a null or empty SPP message"
        );

        return ESP_ERR_INVALID_ARG;
    }

    if (
        s_outbound_queue == NULL ||
        s_writer_events == NULL ||
        s_writer_task_handle == NULL
    ) {
        ESP_LOGE(
            TAG,
            "SPP writer is not initialized"
        );

        return ESP_ERR_INVALID_STATE;
    }

    const size_t message_length =
        strlen(
            message
        );

    if (
        message_length + 2 >
        BT_SPP_TX_BUFFER_SIZE
    ) {
        ESP_LOGE(
            TAG,
            "SPP message too large: %zu bytes, maximum=%d",
            message_length,
            BT_SPP_TX_BUFFER_SIZE - 2
        );

        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t current_session_id;
    int session_available;

    portENTER_CRITICAL(
        &s_state_lock
    );

    current_session_id =
        s_session_id;

    session_available =
        s_spp_server_ready &&
        s_spp_connected &&
        s_spp_handle != 0;

    portEXIT_CRITICAL(
        &s_state_lock
    );

    if (!session_available) {
        ESP_LOGW(
            TAG,
            "Cannot queue SPP message: no active session"
        );

        return ESP_ERR_INVALID_STATE;
    }

    bt_spp_outbound_item_t item;

    memset(
        &item,
        0,
        sizeof(item)
    );

    memcpy(
        item.message,
        message,
        message_length
    );

    item.message[
        message_length
    ] = '\n';

    item.message[
        message_length + 1
    ] = '\0';

    item.length =
        message_length + 1;

    item.session_id =
        current_session_id;

    if (
        xQueueSend(
            s_outbound_queue,
            &item,
            0
        ) !=
        pdTRUE
    ) {
        ESP_LOGE(
            TAG,
            "SPP outbound queue is full"
        );

        return ESP_ERR_NO_MEM;
    }

    ESP_LOGD(
        TAG,
        "SPP message queued, len=%zu, session=%" PRIu32,
        item.length,
        item.session_id
    );

    return ESP_OK;
}

// ------------------------------------------------------------
// END OF CHUNK 2
// Paste Chunk 3 immediately below this line.
// ------------------------------------------------------------
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

        int reject_connection =
            0;

        uint32_t accepted_session_id =
            0;

        portENTER_CRITICAL(
            &s_state_lock
        );

        if (
            !s_spp_server_ready ||
            param->srv_open.status !=
                ESP_SPP_SUCCESS ||
            (
                s_spp_connected &&
                s_spp_handle != 0
            )
        ) {
            reject_connection =
                1;
        } else {
            s_spp_connected =
                1;

            s_spp_handle =
                new_handle;

            s_spp_congested =
                0;

            s_session_id +=
                1;

            if (s_session_id == 0) {
                s_session_id =
                    1;
            }

            accepted_session_id =
                s_session_id;
        }

        portEXIT_CRITICAL(
            &s_state_lock
        );

        if (reject_connection) {
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

            if (disconnect_result != ESP_OK) {
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

        bt_spp_reset_outbound_queue();

        /*
         * Do not clear WRITE_FAILED here. A writer associated with
         * the previous session may still need to consume the
         * cancellation signal.
         */
        if (s_writer_events != NULL) {
            xEventGroupSetBits(
                s_writer_events,
                BT_SPP_WRITER_EVENT_CONNECTED
            );
        }

        bt_spp_update_writer_availability();

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

        uint32_t active_handle;

        portENTER_CRITICAL(
            &s_state_lock
        );

        active_handle =
            s_spp_handle;

        portEXIT_CRITICAL(
            &s_state_lock
        );

        if (
            closed_handle !=
            active_handle
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

        bt_spp_cancel_active_session();

        break;
    }

    case ESP_SPP_DATA_IND_EVT: {
        uint32_t active_handle;
        int active_session;

        portENTER_CRITICAL(
            &s_state_lock
        );

        active_handle =
            s_spp_handle;

        active_session =
            s_spp_server_ready &&
            s_spp_connected &&
            active_handle != 0;

        portEXIT_CRITICAL(
            &s_state_lock
        );

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
        uint32_t active_handle;
        int event_matches_active_handle;

        portENTER_CRITICAL(
            &s_state_lock
        );

        active_handle =
            s_spp_handle;

        event_matches_active_handle =
            param->cong.handle ==
            active_handle;

        if (event_matches_active_handle) {
            s_spp_congested =
                param->cong.cong
                    ? 1
                    : 0;
        }

        portEXIT_CRITICAL(
            &s_state_lock
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

        bt_spp_update_writer_availability();

        break;
    }

    case ESP_SPP_WRITE_EVT: {
        int matches_inflight =
            0;

        uint32_t inflight_session_id =
            0;

        portENTER_CRITICAL(
            &s_state_lock
        );

        if (
            s_write_in_progress &&
            param->write.handle ==
                s_inflight_handle
        ) {
            matches_inflight =
                1;

            inflight_session_id =
                s_inflight_session_id;

            if (
                s_spp_connected &&
                s_spp_handle ==
                    param->write.handle &&
                s_session_id ==
                    inflight_session_id
            ) {
                s_spp_congested =
                    param->write.cong
                        ? 1
                        : 0;
            }
        }

        portEXIT_CRITICAL(
            &s_state_lock
        );

        if (!matches_inflight) {
            ESP_LOGW(
                TAG,
                "Ignoring late or unrelated SPP write event, "
                "handle=%" PRIu32,
                param->write.handle
            );

            break;
        }

        if (
            param->write.status ==
            ESP_SPP_SUCCESS
        ) {
            ESP_LOGI(
                TAG,
                "SPP write completed, len=%d, "
                "session=%" PRIu32,
                param->write.len,
                inflight_session_id
            );

            if (s_writer_events != NULL) {
                xEventGroupSetBits(
                    s_writer_events,
                    BT_SPP_WRITER_EVENT_WRITE_COMPLETE
                );
            }
        } else {
            ESP_LOGE(
                TAG,
                "SPP write failed, status=%d, "
                "session=%" PRIu32,
                param->write.status,
                inflight_session_id
            );

            bt_spp_signal_write_failure();
        }

        break;
    }

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
// WRITER CREATION
// ------------------------------------------------------------

static esp_err_t bt_spp_create_writer(void)
{
    if (
        s_outbound_queue != NULL ||
        s_writer_events != NULL ||
        s_writer_task_handle != NULL
    ) {
        ESP_LOGW(
            TAG,
            "SPP writer pipeline is already initialized"
        );

        return ESP_ERR_INVALID_STATE;
    }

    s_outbound_queue =
        xQueueCreate(
            BT_SPP_TX_QUEUE_LENGTH,
            sizeof(
                bt_spp_outbound_item_t
            )
        );

    if (s_outbound_queue == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create SPP outbound queue"
        );

        return ESP_ERR_NO_MEM;
    }

    s_writer_events =
        xEventGroupCreate();

    if (s_writer_events == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create SPP writer event group"
        );

        vQueueDelete(
            s_outbound_queue
        );

        s_outbound_queue =
            NULL;

        return ESP_ERR_NO_MEM;
    }

    const BaseType_t task_result =
        xTaskCreate(
            bt_spp_writer_task,
            "bt_spp_writer",
            BT_SPP_WRITER_STACK_SIZE,
            NULL,
            BT_SPP_WRITER_PRIORITY,
            &s_writer_task_handle
        );

    if (task_result != pdPASS) {
        ESP_LOGE(
            TAG,
            "Failed to start SPP writer task"
        );

        vEventGroupDelete(
            s_writer_events
        );

        s_writer_events =
            NULL;

        vQueueDelete(
            s_outbound_queue
        );

        s_outbound_queue =
            NULL;

        s_writer_task_handle =
            NULL;

        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "Single completion-driven SPP writer ready"
    );

    return ESP_OK;
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

    portENTER_CRITICAL(
        &s_state_lock
    );

    s_spp_handle =
        0;

    s_session_id =
        0;

    s_spp_connected =
        0;

    s_spp_congested =
        0;

    s_spp_server_ready =
        0;

    s_spp_startup_error =
        ESP_ERR_INVALID_STATE;

    s_write_in_progress =
        0;

    s_inflight_handle =
        0;

    s_inflight_session_id =
        0;

    portEXIT_CRITICAL(
        &s_state_lock
    );

    const esp_err_t readiness_result =
        bt_spp_create_readiness_events();

    if (readiness_result != ESP_OK) {
        return readiness_result;
    }

    const esp_err_t writer_result =
        bt_spp_create_writer();

    if (writer_result != ESP_OK) {
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