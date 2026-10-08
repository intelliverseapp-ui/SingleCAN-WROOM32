#include "bt_spp_writer.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char *TAG =
    "BT_SPP_WRITER";

#define BT_SPP_TX_BUFFER_SIZE 256
#define BT_SPP_TX_QUEUE_LENGTH 32

#define BT_SPP_WRITER_STACK_SIZE 4096
#define BT_SPP_WRITER_PRIORITY 6

#define BT_SPP_WRITE_TIMEOUT_MS 5000

#define BT_SPP_WRITER_EVENT_CONNECTED BIT0
#define BT_SPP_WRITER_EVENT_WRITABLE BIT1
#define BT_SPP_WRITER_EVENT_WRITE_COMPLETE BIT2
#define BT_SPP_WRITER_EVENT_WRITE_FAILED BIT3

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

static TaskHandle_t s_writer_task_handle =
    NULL;

static bt_spp_writer_session_provider_t s_session_provider =
    NULL;

static bt_spp_writer_disconnect_handler_t s_disconnect_handler =
    NULL;

static portMUX_TYPE s_writer_lock =
    portMUX_INITIALIZER_UNLOCKED;

static volatile int s_write_in_progress =
    0;

static volatile uint32_t s_inflight_handle =
    0;

static volatile uint32_t s_inflight_session_id =
    0;

static uint32_t s_queue_full_count =
    0;

static uint32_t s_immediate_write_failure_count =
    0;

static uint32_t s_asynchronous_write_failure_count =
    0;

static uint32_t s_write_timeout_count =
    0;

// ------------------------------------------------------------
// FORWARD DECLARATIONS
// ------------------------------------------------------------

static void bt_spp_writer_task(
    void *task_argument
);

// ------------------------------------------------------------
// DELIVERY-FAILURE ACCOUNTING
// ------------------------------------------------------------

static uint32_t increment_counter(
    uint32_t *counter
)
{
    if (counter == NULL) {
        return 0;
    }

    uint32_t value;

    portENTER_CRITICAL(
        &s_writer_lock
    );

    *counter +=
        1;

    value =
        *counter;

    portEXIT_CRITICAL(
        &s_writer_lock
    );

    return value;
}

static void disconnect_failed_session(
    uint32_t handle,
    uint32_t session_id
)
{
    if (
        handle == 0 ||
        session_id == 0 ||
        s_disconnect_handler == NULL
    ) {
        return;
    }

    s_disconnect_handler(
        handle,
        session_id
    );
}

// ------------------------------------------------------------
// SESSION SNAPSHOT
// ------------------------------------------------------------

static int get_session_snapshot(
    bt_spp_writer_session_t *session
)
{
    if (
        session == NULL ||
        s_session_provider == NULL
    ) {
        return 0;
    }

    memset(
        session,
        0,
        sizeof(*session)
    );

    s_session_provider(
        session
    );

    return 1;
}

// ------------------------------------------------------------
// IN-FLIGHT WRITE OWNERSHIP
// ------------------------------------------------------------

static int claim_inflight_write(
    uint32_t handle,
    uint32_t session_id
)
{
    int claimed =
        0;

    portENTER_CRITICAL(
        &s_writer_lock
    );

    if (!s_write_in_progress) {
        s_write_in_progress =
            1;

        s_inflight_handle =
            handle;

        s_inflight_session_id =
            session_id;

        claimed =
            1;
    }

    portEXIT_CRITICAL(
        &s_writer_lock
    );

    return claimed;
}

static void clear_inflight_write(
    uint32_t expected_handle,
    uint32_t expected_session_id
)
{
    portENTER_CRITICAL(
        &s_writer_lock
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
        &s_writer_lock
    );
}

static int write_is_in_progress(void)
{
    int in_progress;

    portENTER_CRITICAL(
        &s_writer_lock
    );

    in_progress =
        s_write_in_progress;

    portEXIT_CRITICAL(
        &s_writer_lock
    );

    return in_progress;
}

// ------------------------------------------------------------
// WRITER AVAILABILITY
// ------------------------------------------------------------

static void update_writer_availability(void)
{
    if (
        s_writer_events ==
        NULL
    ) {
        return;
    }

    bt_spp_writer_session_t session;

    if (
        !get_session_snapshot(
            &session
        )
    ) {
        xEventGroupClearBits(
            s_writer_events,
            BT_SPP_WRITER_EVENT_CONNECTED |
                BT_SPP_WRITER_EVENT_WRITABLE
        );

        return;
    }

    if (
        session.server_ready &&
        session.connected &&
        session.handle != 0 &&
        !session.congested &&
        !write_is_in_progress()
    ) {
        xEventGroupSetBits(
            s_writer_events,
            BT_SPP_WRITER_EVENT_CONNECTED |
                BT_SPP_WRITER_EVENT_WRITABLE
        );

        return;
    }

    xEventGroupClearBits(
        s_writer_events,
        BT_SPP_WRITER_EVENT_WRITABLE
    );

    if (
        !session.connected ||
        session.handle == 0
    ) {
        xEventGroupClearBits(
            s_writer_events,
            BT_SPP_WRITER_EVENT_CONNECTED
        );
    }
}

// ------------------------------------------------------------
// WRITER CANCELLATION
// ------------------------------------------------------------

static void signal_write_failure(void)
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

static void reset_outbound_queue(void)
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

// ------------------------------------------------------------
// COMPLETION-DRIVEN WRITER TASK
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

        bt_spp_writer_session_t session;

        if (
            !get_session_snapshot(
                &session
            )
        ) {
            ESP_LOGE(
                TAG,
                "Unable to obtain active SPP session"
            );

            memset(
                &item,
                0,
                sizeof(item)
            );

            continue;
        }

        const int session_is_valid =
            session.server_ready &&
            session.connected &&
            session.handle != 0 &&
            !session.congested &&
            item.session_id ==
                session.session_id;

        if (!session_is_valid) {
            ESP_LOGW(
                TAG,
                "Queued message discarded because session changed"
            );

            memset(
                &item,
                0,
                sizeof(item)
            );

            update_writer_availability();

            continue;
        }

        if (
            !claim_inflight_write(
                session.handle,
                session.session_id
            )
        ) {
            ESP_LOGE(
                TAG,
                "Unable to claim SPP write ownership"
            );

            memset(
                &item,
                0,
                sizeof(item)
            );

            update_writer_availability();

            continue;
        }

        /*
         * Clear completion state only after ownership of this exact
         * write has been recorded.
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
            session.session_id
        );

        const esp_err_t write_result =
            esp_spp_write(
                session.handle,
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

            const uint32_t failure_count =
                increment_counter(
                    &s_immediate_write_failure_count
                );

            ESP_LOGE(
                TAG,
                "Immediate SPP write failures=%" PRIu32
                "; closing affected session",
                failure_count
            );

            clear_inflight_write(
                session.handle,
                session.session_id
            );

            disconnect_failed_session(
                session.handle,
                session.session_id
            );

            update_writer_availability();

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
            const uint32_t failure_count =
                increment_counter(
                    &s_asynchronous_write_failure_count
                );

            ESP_LOGE(
                TAG,
                "Serialized SPP write failed or session closed; "
                "asynchronous failures=%" PRIu32,
                failure_count
            );

            disconnect_failed_session(
                session.handle,
                session.session_id
            );
        } else {
            const uint32_t timeout_count =
                increment_counter(
                    &s_write_timeout_count
                );

            ESP_LOGE(
                TAG,
                "Serialized SPP write timed out; "
                "timeouts=%" PRIu32,
                timeout_count
            );

            bt_spp_writer_session_t current_session;

            if (
                get_session_snapshot(
                    &current_session
                ) &&
                current_session.connected &&
                current_session.handle ==
                    session.handle &&
                current_session.session_id ==
                    session.session_id &&
                s_disconnect_handler !=
                    NULL
            ) {
                s_disconnect_handler(
                    session.handle,
                    session.session_id
                );
            }
        }

        clear_inflight_write(
            session.handle,
            session.session_id
        );

        update_writer_availability();

        memset(
            &item,
            0,
            sizeof(item)
        );
    }
}

// ------------------------------------------------------------
// END OF WRITER CHUNK 1
// Paste Writer Chunk 2 immediately below this line.
// ------------------------------------------------------------
// ------------------------------------------------------------
// PUBLIC WRITER INITIALIZATION
// ------------------------------------------------------------

esp_err_t bt_spp_writer_init(
    bt_spp_writer_session_provider_t session_provider,
    bt_spp_writer_disconnect_handler_t disconnect_handler
)
{
    if (
        session_provider == NULL ||
        disconnect_handler == NULL
    ) {
        ESP_LOGE(
            TAG,
            "Cannot initialize writer without required callbacks"
        );

        return ESP_ERR_INVALID_ARG;
    }

    if (
        s_outbound_queue != NULL ||
        s_writer_events != NULL ||
        s_writer_task_handle != NULL
    ) {
        ESP_LOGW(
            TAG,
            "SPP writer is already initialized"
        );

        return ESP_ERR_INVALID_STATE;
    }

    s_session_provider =
        session_provider;

    s_disconnect_handler =
        disconnect_handler;

    portENTER_CRITICAL(
        &s_writer_lock
    );

    s_write_in_progress =
        0;

    s_inflight_handle =
        0;

    s_inflight_session_id =
        0;

    s_queue_full_count =
        0;

    s_immediate_write_failure_count =
        0;

    s_asynchronous_write_failure_count =
        0;

    s_write_timeout_count =
        0;

    portEXIT_CRITICAL(
        &s_writer_lock
    );

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

        s_session_provider =
            NULL;

        s_disconnect_handler =
            NULL;

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

        s_session_provider =
            NULL;

        s_disconnect_handler =
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

        s_session_provider =
            NULL;

        s_disconnect_handler =
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
// PUBLIC WRITER SEND
// ------------------------------------------------------------

esp_err_t bt_spp_writer_send(
    const char *message,
    uint32_t session_id
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
        s_writer_task_handle == NULL ||
        s_session_provider == NULL
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

    bt_spp_writer_session_t current_session;

    if (
        !get_session_snapshot(
            &current_session
        ) ||
        !current_session.server_ready ||
        !current_session.connected ||
        current_session.handle == 0 ||
        current_session.session_id !=
            session_id
    ) {
        ESP_LOGW(
            TAG,
            "Cannot queue SPP message: session is unavailable"
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
        session_id;

    if (
        xQueueSend(
            s_outbound_queue,
            &item,
            0
        ) !=
        pdTRUE
    ) {
        const uint32_t failure_count =
            increment_counter(
                &s_queue_full_count
            );

        ESP_LOGE(
            TAG,
            "SPP outbound queue is full; drops=%" PRIu32
            "; closing affected session",
            failure_count
        );

        disconnect_failed_session(
            current_session.handle,
            current_session.session_id
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
// PUBLIC CONNECTION NOTIFICATIONS
// ------------------------------------------------------------

void bt_spp_writer_on_connected(void)
{
    if (
        s_writer_events ==
        NULL
    ) {
        return;
    }

    /*
     * Do not clear WRITE_FAILED here. A writer associated with the
     * previous session may still need to consume the cancellation
     * signal.
     */
    xEventGroupSetBits(
        s_writer_events,
        BT_SPP_WRITER_EVENT_CONNECTED
    );

    update_writer_availability();
}

void bt_spp_writer_on_disconnected(void)
{
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

    reset_outbound_queue();

    /*
     * Wake an in-flight writer immediately. The signal remains set
     * until the waiting writer consumes it.
     */
    signal_write_failure();
}

void bt_spp_writer_on_congestion_changed(void)
{
    update_writer_availability();
}

// ------------------------------------------------------------
// PUBLIC WRITE-EVENT DELIVERY
// ------------------------------------------------------------

int bt_spp_writer_on_write_event(
    const esp_spp_cb_param_t *parameters
)
{
    if (
        parameters == NULL ||
        s_writer_events == NULL
    ) {
        return 0;
    }

    int matches_inflight =
        0;

    uint32_t inflight_session_id =
        0;

    portENTER_CRITICAL(
        &s_writer_lock
    );

    if (
        s_write_in_progress &&
        parameters->write.handle ==
            s_inflight_handle
    ) {
        matches_inflight =
            1;

        inflight_session_id =
            s_inflight_session_id;
    }

    portEXIT_CRITICAL(
        &s_writer_lock
    );

    if (!matches_inflight) {
        ESP_LOGW(
            TAG,
            "Ignoring late or unrelated SPP write event, "
            "handle=%" PRIu32,
            parameters->write.handle
        );

        return 0;
    }

    if (
        parameters->write.status ==
        ESP_SPP_SUCCESS
    ) {
        ESP_LOGI(
            TAG,
            "SPP write completed, len=%d, session=%" PRIu32,
            parameters->write.len,
            inflight_session_id
        );

        xEventGroupSetBits(
            s_writer_events,
            BT_SPP_WRITER_EVENT_WRITE_COMPLETE
        );
    } else {
        ESP_LOGE(
            TAG,
            "SPP write failed, status=%d, session=%" PRIu32,
            parameters->write.status,
            inflight_session_id
        );

        signal_write_failure();
    }

    return 1;
}

// ------------------------------------------------------------
// PUBLIC DELIVERY STATISTICS
// ------------------------------------------------------------

void bt_spp_writer_get_stats(
    bt_spp_writer_stats_t *stats
)
{
    if (stats == NULL) {
        return;
    }

    portENTER_CRITICAL(
        &s_writer_lock
    );

    stats->queue_full =
        s_queue_full_count;

    stats->immediate_write_failures =
        s_immediate_write_failure_count;

    stats->asynchronous_write_failures =
        s_asynchronous_write_failure_count;

    stats->write_timeouts =
        s_write_timeout_count;

    portEXIT_CRITICAL(
        &s_writer_lock
    );
}
