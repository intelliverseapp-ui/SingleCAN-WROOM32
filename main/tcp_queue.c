#include "tcp_queue.h"

#include "bt_spp.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "singlecan_leds.h"

#include <stddef.h>
#include <string.h>

static const char *TAG =
    "SPP_OUTBOUND_QUEUE";

#define SPP_QUEUE_RETRY_DELAY_MS 25
#define SPP_QUEUE_DISCONNECTED_DELAY_MS 100
#define SPP_QUEUE_MAXIMUM_RETRIES 40

// ------------------------------------------------------------
// GLOBAL QUEUE HANDLE
// ------------------------------------------------------------

QueueHandle_t tcp_outbound_queue =
    NULL;

// ------------------------------------------------------------
// INTERNAL: CLEAR QUEUED ITEM
// ------------------------------------------------------------

static void clear_queue_item(
    tcp_queue_item_t *item
)
{
    if (item == NULL) {
        return;
    }

    memset(
        item,
        0,
        sizeof(*item)
    );
}

// ------------------------------------------------------------
// PUSH A MESSAGE INTO THE OUTBOUND QUEUE
// ------------------------------------------------------------

void tcp_queue_push(
    const char *line
)
{
    if (
        tcp_outbound_queue == NULL ||
        line == NULL
    ) {
        ESP_LOGE(
            TAG,
            "Outbound queue is unavailable or message is null"
        );

        singlecan_leds_error();

        return;
    }

    const size_t line_length =
        strnlen(
            line,
            TCP_QUEUE_MAX_LINE_LEN
        );

    if (line_length == 0) {
        ESP_LOGW(
            TAG,
            "Empty outbound message rejected"
        );

        return;
    }

    if (
        line_length >=
        TCP_QUEUE_MAX_LINE_LEN
    ) {
        ESP_LOGE(
            TAG,
            "Outbound message exceeds maximum queue length"
        );

        singlecan_leds_error();

        return;
    }

    tcp_queue_item_t item;

    clear_queue_item(
        &item
    );

    memcpy(
        item.line,
        line,
        line_length
    );

    item.line[
        line_length
    ] = '\0';

    item.len =
        line_length;

    const BaseType_t queue_result =
        xQueueSend(
            tcp_outbound_queue,
            &item,
            0
        );

    if (
        queue_result !=
        pdTRUE
    ) {
        ESP_LOGW(
            TAG,
            "Outbound queue is full; message dropped"
        );

        singlecan_leds_error();

        return;
    }

    ESP_LOGD(
        TAG,
        "Outbound message queued, len=%zu",
        item.len
    );
}

// ------------------------------------------------------------
// INTERNAL: SEND ONE QUEUED MESSAGE THROUGH THE SPP OWNER
// ------------------------------------------------------------

static bool send_queued_message(
    const tcp_queue_item_t *item
)
{
    if (
        item == NULL ||
        item->len == 0 ||
        item->line[0] == '\0'
    ) {
        ESP_LOGE(
            TAG,
            "Invalid outbound queue item"
        );

        return false;
    }

    unsigned int retry_count =
        0;

    while (
        retry_count <
        SPP_QUEUE_MAXIMUM_RETRIES
    ) {
        if (
            !bt_spp_is_connected() ||
            bt_spp_get_handle() == 0
        ) {
            ESP_LOGW(
                TAG,
                "No SPP client connected; queued message dropped"
            );

            return false;
        }

        const esp_err_t send_result =
            bt_spp_send(
                item->line
            );

        if (
            send_result ==
            ESP_OK
        ) {
            ESP_LOGI(
                TAG,
                "Outbound message accepted by SPP writer, len=%zu",
                item->len
            );

            return true;
        }

        if (
            send_result !=
            ESP_ERR_INVALID_STATE
        ) {
            ESP_LOGE(
                TAG,
                "Outbound SPP send failed: %s",
                esp_err_to_name(
                    send_result
                )
            );

            return false;
        }

        /*
         * ESP_ERR_INVALID_STATE can mean:
         * - a write is already awaiting completion,
         * - the SPP link is congested, or
         * - the connection disappeared between checks.
         *
         * Recheck the connection and retry briefly. No direct call
         * to esp_spp_write() is made from this task.
         */
        if (
            !bt_spp_is_connected() ||
            bt_spp_get_handle() == 0
        ) {
            ESP_LOGW(
                TAG,
                "SPP connection was lost while waiting to send"
            );

            return false;
        }

        retry_count +=
            1;

        vTaskDelay(
            pdMS_TO_TICKS(
                SPP_QUEUE_RETRY_DELAY_MS
            )
        );
    }

    ESP_LOGE(
        TAG,
        "Outbound message timed out waiting for SPP writer"
    );

    return false;
}

// ------------------------------------------------------------
// QUEUE CONSUMER TASK
// ------------------------------------------------------------

void tcp_queue_task(
    void *arg
)
{
    (void)arg;

    tcp_queue_item_t item;

    clear_queue_item(
        &item
    );

    ESP_LOGI(
        TAG,
        "SPP outbound queue task started"
    );

    while (true) {
        const BaseType_t receive_result =
            xQueueReceive(
                tcp_outbound_queue,
                &item,
                portMAX_DELAY
            );

        if (
            receive_result !=
            pdTRUE
        ) {
            ESP_LOGE(
                TAG,
                "Outbound queue receive failed"
            );

            singlecan_leds_error();

            vTaskDelay(
                pdMS_TO_TICKS(
                    SPP_QUEUE_DISCONNECTED_DELAY_MS
                )
            );

            continue;
        }

        const bool sent =
            send_queued_message(
                &item
            );

        if (!sent) {
            ESP_LOGW(
                TAG,
                "Queued outbound message was not delivered"
            );

            singlecan_leds_error();
        }

        clear_queue_item(
            &item
        );
    }
}

// ------------------------------------------------------------
// INITIALIZATION
// ------------------------------------------------------------

void tcp_queue_init(void)
{
    if (
        tcp_outbound_queue !=
        NULL
    ) {
        ESP_LOGW(
            TAG,
            "Outbound queue is already initialized"
        );

        return;
    }

    ESP_LOGI(
        TAG,
        "Initializing SPP outbound queue"
    );

    tcp_outbound_queue =
        xQueueCreate(
            TCP_QUEUE_LENGTH,
            sizeof(
                tcp_queue_item_t
            )
        );

    if (
        tcp_outbound_queue ==
        NULL
    ) {
        ESP_LOGE(
            TAG,
            "Failed to create SPP outbound queue"
        );

        singlecan_leds_error();

        return;
    }

    const BaseType_t task_result =
        xTaskCreate(
            tcp_queue_task,
            "spp_outbound_queue",
            4096,
            NULL,
            5,
            NULL
        );

    if (
        task_result !=
        pdPASS
    ) {
        ESP_LOGE(
            TAG,
            "Failed to start SPP outbound queue task"
        );

        vQueueDelete(
            tcp_outbound_queue
        );

        tcp_outbound_queue =
            NULL;

        singlecan_leds_error();

        return;
    }

    ESP_LOGI(
        TAG,
        "SPP outbound queue ready"
    );
}