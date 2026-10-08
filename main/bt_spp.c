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
#include "singlecan_commands.h"

static const char *TAG =
    "BT_SPP";

static const char *BT_DEVICE_NAME =
    "BabyNodeCAN";

#define BT_SPP_TX_BUFFER_SIZE 256
#define BT_SPP_TX_QUEUE_LENGTH 32

#define BT_SPP_MAX_FRAME_LENGTH 4096

#define BT_SPP_RX_BUFFER_SIZE \
    (BT_SPP_MAX_FRAME_LENGTH + 1)

#define BT_SPP_WRITER_STACK_SIZE 4096
#define BT_SPP_WRITER_PRIORITY 6

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

static volatile uint32_t s_spp_handle =
    0;

static volatile uint32_t s_session_id =
    0;

static volatile int s_spp_connected =
    0;

static volatile int s_spp_congested =
    0;

static volatile int s_spp_write_in_progress =
    0;

static volatile int s_spp_server_ready =
    0;

static volatile esp_err_t s_spp_startup_error =
    ESP_ERR_INVALID_STATE;

static char s_rx_buffer[
    BT_SPP_RX_BUFFER_SIZE
];

static size_t s_rx_length =
    0;

static int s_rx_discard_until_newline =
    0;

// ------------------------------------------------------------
// FORWARD DECLARATIONS
// ------------------------------------------------------------

static void bt_spp_start_next_writer_state(void);

static void bt_spp_writer_task(
    void *task_argument
);

static void spp_event_handler(
    esp_spp_cb_event_t event,
    esp_spp_cb_param_t *param
);

// ------------------------------------------------------------
// SERVER READINESS HELPERS
// ------------------------------------------------------------

static void bt_spp_reset_readiness_state(void)
{
    s_spp_server_ready =
        0;

    s_spp_startup_error =
        ESP_ERR_INVALID_STATE;

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
    s_spp_server_ready =
        1;

    s_spp_startup_error =
        ESP_OK;

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
    s_spp_server_ready =
        0;

    s_spp_startup_error =
        error;

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
// RECEIVE-FRAMING HELPERS
// ------------------------------------------------------------

static void bt_spp_reset_receive_state(void)
{
    memset(
        s_rx_buffer,
        0,
        sizeof(s_rx_buffer)
    );

    s_rx_length =
        0;

    s_rx_discard_until_newline =
        0;
}

static int bt_spp_is_allowed_input_byte(
    uint8_t byte
)
{
    if (
        byte == '\n' ||
        byte == '\r' ||
        byte == '\t'
    ) {
        return 1;
    }

    if (
        byte >= 0x20 &&
        byte <= 0x7E
    ) {
        return 1;
    }

    return 0;
}

static void bt_spp_discard_current_frame(
    const char *reason
)
{
    ESP_LOGW(
        TAG,
        "Discarding invalid SPP frame: %s",
        reason
    );

    memset(
        s_rx_buffer,
        0,
        sizeof(s_rx_buffer)
    );

    s_rx_length =
        0;

    s_rx_discard_until_newline =
        1;
}

static void bt_spp_process_complete_frame(void)
{
    if (s_rx_length == 0) {
        return;
    }

    s_rx_buffer[
        s_rx_length
    ] = '\0';

    ESP_LOGI(
        TAG,
        "Complete SPP frame received, len=%zu",
        s_rx_length
    );

    singlecan_commands_process(
        s_rx_buffer
    );

    memset(
        s_rx_buffer,
        0,
        sizeof(s_rx_buffer)
    );

    s_rx_length =
        0;
}

static void bt_spp_process_received_bytes(
    const uint8_t *data,
    size_t data_length
)
{
    if (
        data == NULL ||
        data_length == 0
    ) {
        ESP_LOGW(
            TAG,
            "SPP data event contained no data"
        );

        return;
    }

    for (
        size_t index = 0;
        index < data_length;
        ++index
    ) {
        const uint8_t byte =
            data[index];

        if (s_rx_discard_until_newline) {
            if (byte == '\n') {
                s_rx_discard_until_newline =
                    0;

                s_rx_length =
                    0;

                memset(
                    s_rx_buffer,
                    0,
                    sizeof(s_rx_buffer)
                );

                ESP_LOGI(
                    TAG,
                    "SPP frame discard completed at newline"
                );
            }

            continue;
        }

        if (
            !bt_spp_is_allowed_input_byte(
                byte
            )
        ) {
            bt_spp_discard_current_frame(
                "binary or control byte detected"
            );

            continue;
        }

        if (byte == '\r') {
            continue;
        }

        if (byte == '\n') {
            bt_spp_process_complete_frame();

            continue;
        }

        if (
            s_rx_length >=
            BT_SPP_MAX_FRAME_LENGTH
        ) {
            bt_spp_discard_current_frame(
                "maximum frame length exceeded"
            );

            continue;
        }

        s_rx_buffer[
            s_rx_length
        ] = (char)byte;

        s_rx_length +=
            1;
    }
}

// ------------------------------------------------------------
// OUTBOUND WRITER HELPERS
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

static void bt_spp_clear_writer_events(void)
{
    if (
        s_writer_events ==
        NULL
    ) {
        return;
    }

    xEventGroupClearBits(
        s_writer_events,
        BT_SPP_WRITER_EVENT_CONNECTED |
            BT_SPP_WRITER_EVENT_WRITABLE |
            BT_SPP_WRITER_EVENT_WRITE_COMPLETE |
            BT_SPP_WRITER_EVENT_WRITE_FAILED
    );
}

static void bt_spp_mark_connected(void)
{
    if (
        s_writer_events ==
        NULL
    ) {
        return;
    }

    xEventGroupClearBits(
        s_writer_events,
        BT_SPP_WRITER_EVENT_WRITE_COMPLETE |
            BT_SPP_WRITER_EVENT_WRITE_FAILED
    );

    xEventGroupSetBits(
        s_writer_events,
        BT_SPP_WRITER_EVENT_CONNECTED |
            BT_SPP_WRITER_EVENT_WRITABLE
    );
}

static void bt_spp_mark_disconnected(void)
{
    if (
        s_writer_events ==
        NULL
    ) {
        return;
    }

    xEventGroupClearBits(
        s_writer_events,
        BT_SPP_WRITER_EVENT_CONNECTED |
            BT_SPP_WRITER_EVENT_WRITABLE |
            BT_SPP_WRITER_EVENT_WRITE_COMPLETE
    );

    xEventGroupSetBits(
        s_writer_events,
        BT_SPP_WRITER_EVENT_WRITE_FAILED
    );
}

static void bt_spp_start_next_writer_state(void)
{
    if (
        s_writer_events ==
        NULL
    ) {
        return;
    }

    if (
        s_spp_server_ready &&
        s_spp_connected &&
        s_spp_handle != 0 &&
        !s_spp_congested &&
        !s_spp_write_in_progress
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
    }
}

// ------------------------------------------------------------
// SINGLE COMPLETION-DRIVEN SPP WRITER TASK
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
        const BaseType_t queue_result =
            xQueueReceive(
                s_outbound_queue,
                &item,
                portMAX_DELAY
            );

        if (
            queue_result !=
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

        if (
            !s_spp_server_ready ||
            !s_spp_connected ||
            s_spp_handle == 0
        ) {
            ESP_LOGW(
                TAG,
                "Queued message discarded because SPP is unavailable"
            );

            memset(
                &item,
                0,
                sizeof(item)
            );

            continue;
        }

        if (
            item.session_id !=
            s_session_id
        ) {
            ESP_LOGW(
                TAG,
                "Queued message discarded because SPP session changed"
            );

            memset(
                &item,
                0,
                sizeof(item)
            );

            continue;
        }

        const uint32_t write_handle =
            s_spp_handle;

        xEventGroupClearBits(
            s_writer_events,
            BT_SPP_WRITER_EVENT_WRITABLE |
                BT_SPP_WRITER_EVENT_WRITE_COMPLETE |
                BT_SPP_WRITER_EVENT_WRITE_FAILED
        );

        s_spp_write_in_progress =
            1;

        ESP_LOGI(
            TAG,
            "Submitting serialized SPP write, len=%zu",
            item.length
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

            s_spp_write_in_progress =
                0;

            bt_spp_start_next_writer_state();

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
                portMAX_DELAY
            );

        if (
            completion_bits &
            BT_SPP_WRITER_EVENT_WRITE_FAILED
        ) {
            ESP_LOGE(
                TAG,
                "Serialized SPP write failed or connection closed"
            );
        } else if (
            completion_bits &
            BT_SPP_WRITER_EVENT_WRITE_COMPLETE
        ) {
            ESP_LOGI(
                TAG,
                "Serialized SPP write completed"
            );
        }

        s_spp_write_in_progress =
            0;

        bt_spp_start_next_writer_state();

        memset(
            &item,
            0,
            sizeof(item)
        );
    }
}

// ------------------------------------------------------------
// PUBLIC SERVER READINESS
// ------------------------------------------------------------

int bt_spp_is_server_ready(void)
{
    return s_spp_server_ready;
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

    const TickType_t timeout_ticks =
        pdMS_TO_TICKS(
            timeout_ms
        );

    const EventBits_t result_bits =
        xEventGroupWaitBits(
            s_readiness_events,
            BT_SPP_READINESS_EVENT_READY |
                BT_SPP_READINESS_EVENT_FAILED,
            pdFALSE,
            pdFALSE,
            timeout_ticks
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
        if (
            s_spp_startup_error !=
            ESP_OK
        ) {
            return s_spp_startup_error;
        }

        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGE(
        TAG,
        "Timed out waiting for SPP server readiness"
    );

    return ESP_ERR_TIMEOUT;
}

// ------------------------------------------------------------
// PUBLIC CONNECTION ACCESSORS
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
        !s_spp_server_ready ||
        s_outbound_queue == NULL ||
        s_writer_events == NULL ||
        s_writer_task_handle == NULL
    ) {
        ESP_LOGE(
            TAG,
            "SPP server or writer is not ready"
        );

        return ESP_ERR_INVALID_STATE;
    }

    if (
        !s_spp_connected ||
        s_spp_handle == 0
    ) {
        ESP_LOGW(
            TAG,
            "Cannot queue SPP message: no client connected"
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
        s_session_id;

    const BaseType_t queue_result =
        xQueueSend(
            s_outbound_queue,
            &item,
            0
        );

    if (
        queue_result !=
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

        if (
            result !=
            ESP_OK
        ) {
            ESP_LOGE(
                TAG,
                "esp_bt_gap_set_device_name failed: %s",
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

        if (
            result !=
            ESP_OK
        ) {
            ESP_LOGE(
                TAG,
                "esp_bt_gap_set_scan_mode failed: %s",
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

        if (
            result !=
            ESP_OK
        ) {
            ESP_LOGE(
                TAG,
                "esp_spp_start_srv failed: %s",
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

    case ESP_SPP_SRV_OPEN_EVT:
        if (!s_spp_server_ready) {
            ESP_LOGE(
                TAG,
                "Rejecting SPP open event before server readiness"
            );

            break;
        }

        s_session_id +=
            1;

        if (s_session_id == 0) {
            s_session_id =
                1;
        }

        s_spp_handle =
            param->srv_open.handle;

        s_spp_connected =
            1;

        s_spp_congested =
            0;

        s_spp_write_in_progress =
            0;

        singlecan_commands_reset_session();

        bt_spp_reset_receive_state();

        bt_spp_reset_outbound_queue();

        bt_spp_clear_writer_events();

        bt_spp_mark_connected();

        ESP_LOGI(
            TAG,
            "SPP client connected, handle=%" PRIu32
            ", session=%" PRIu32,
            s_spp_handle,
            s_session_id
        );
        break;

    case ESP_SPP_CLOSE_EVT:
        if (
            param->close.handle !=
            s_spp_handle
        ) {
            ESP_LOGW(
                TAG,
                "Ignoring close event from stale handle=%" PRIu32,
                param->close.handle
            );

            break;
        }

        ESP_LOGI(
            TAG,
            "SPP connection closed, handle=%" PRIu32,
            param->close.handle
        );

        s_spp_connected =
            0;

        s_spp_handle =
            0;

        s_spp_congested =
            0;

        s_spp_write_in_progress =
            0;

        s_session_id +=
            1;

        if (s_session_id == 0) {
            s_session_id =
                1;
        }

        singlecan_commands_reset_session();

        bt_spp_reset_receive_state();

        bt_spp_reset_outbound_queue();

        bt_spp_mark_disconnected();
        break;

    case ESP_SPP_DATA_IND_EVT:
        if (
            !s_spp_server_ready ||
            !s_spp_connected ||
            s_spp_handle == 0
        ) {
            ESP_LOGW(
                TAG,
                "Ignoring SPP data without an active ready session"
            );

            break;
        }

        if (
            param->data_ind.handle !=
            s_spp_handle
        ) {
            ESP_LOGW(
                TAG,
                "Ignoring SPP data from stale handle=%" PRIu32,
                param->data_ind.handle
            );

            break;
        }

        if (
            param->data_ind.len <= 0 ||
            param->data_ind.data == NULL
        ) {
            ESP_LOGW(
                TAG,
                "SPP data event contained no data"
            );

            break;
        }

        ESP_LOGI(
            TAG,
            "SPP data received, len=%d",
            param->data_ind.len
        );

        bt_spp_process_received_bytes(
            param->data_ind.data,
            (size_t)param->data_ind.len
        );
        break;

    case ESP_SPP_CONG_EVT:
        if (
            param->cong.handle !=
            s_spp_handle
        ) {
            ESP_LOGW(
                TAG,
                "Ignoring congestion event from stale handle=%" PRIu32,
                param->cong.handle
            );

            break;
        }

        s_spp_congested =
            param->cong.cong
                ? 1
                : 0;

        ESP_LOGI(
            TAG,
            "SPP congestion changed, congested=%d",
            s_spp_congested
        );

        bt_spp_start_next_writer_state();
        break;

    case ESP_SPP_WRITE_EVT:
        if (
            param->write.handle !=
            s_spp_handle
        ) {
            ESP_LOGW(
                TAG,
                "Ignoring write event from stale handle=%" PRIu32,
                param->write.handle
            );

            break;
        }

        s_spp_write_in_progress =
            0;

        s_spp_congested =
            param->write.cong
                ? 1
                : 0;

        if (
            param->write.status ==
            ESP_SPP_SUCCESS
        ) {
            ESP_LOGI(
                TAG,
                "SPP write event succeeded, len=%d, congested=%d",
                param->write.len,
                param->write.cong
            );

            if (
                s_writer_events !=
                NULL
            ) {
                xEventGroupSetBits(
                    s_writer_events,
                    BT_SPP_WRITER_EVENT_WRITE_COMPLETE
                );
            }
        } else {
            ESP_LOGE(
                TAG,
                "SPP write event failed, status=%d",
                param->write.status
            );

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

        bt_spp_start_next_writer_state();
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
// CREATE THE SINGLE SPP WRITER PIPELINE
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

    if (
        s_outbound_queue ==
        NULL
    ) {
        ESP_LOGE(
            TAG,
            "Failed to create SPP outbound queue"
        );

        return ESP_ERR_NO_MEM;
    }

    s_writer_events =
        xEventGroupCreate();

    if (
        s_writer_events ==
        NULL
    ) {
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

    if (
        task_result !=
        pdPASS
    ) {
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
// BLUETOOTH CLASSIC AND SPP INITIALIZATION
// ------------------------------------------------------------

esp_err_t bt_spp_init(void)
{
    bt_spp_reset_receive_state();

    singlecan_commands_reset_session();

    s_spp_handle =
        0;

    s_session_id =
        0;

    s_spp_connected =
        0;

    s_spp_congested =
        0;

    s_spp_write_in_progress =
        0;

    s_spp_server_ready =
        0;

    s_spp_startup_error =
        ESP_ERR_INVALID_STATE;

    const esp_err_t readiness_result =
        bt_spp_create_readiness_events();

    if (
        readiness_result !=
        ESP_OK
    ) {
        return readiness_result;
    }

    const esp_err_t writer_result =
        bt_spp_create_writer();

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

    if (
        result !=
        ESP_OK
    ) {
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

    if (
        result !=
        ESP_OK
    ) {
        ESP_LOGE(
            TAG,
            "Bluetooth controller init failed: %s",
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

    if (
        result !=
        ESP_OK
    ) {
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

    if (
        result !=
        ESP_OK
    ) {
        ESP_LOGE(
            TAG,
            "Bluedroid init failed: %s",
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

    if (
        result !=
        ESP_OK
    ) {
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

    if (
        result !=
        ESP_OK
    ) {
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

    if (
        result !=
        ESP_OK
    ) {
        ESP_LOGE(
            TAG,
            "SPP enhanced init failed: %s",
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