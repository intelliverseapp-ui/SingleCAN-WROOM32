#include "bt_spp_rejected_client.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char *TAG =
    "BT_SPP_REJECT";

#define BT_SPP_REJECTED_QUEUE_LENGTH 8
#define BT_SPP_REJECTED_SLOT_COUNT 4

#define BT_SPP_REJECTED_TASK_STACK_SIZE 3072
#define BT_SPP_REJECTED_TASK_PRIORITY 5

#define BT_SPP_REJECTED_RETRY_DELAY_MS 250
#define BT_SPP_REJECTED_MAXIMUM_ATTEMPTS 3

typedef struct {
    uint32_t handle;
    uint32_t token;
    uint32_t attempt;
} bt_spp_rejected_work_t;

typedef struct {
    uint32_t handle;
    uint32_t token;
    int active;
} bt_spp_rejected_slot_t;

static QueueHandle_t s_retry_queue =
    NULL;

static TaskHandle_t s_retry_task =
    NULL;

static bt_spp_rejected_client_disconnect_t s_disconnect_callback =
    NULL;

static bt_spp_rejected_client_active_handle_t s_active_handle_callback =
    NULL;

static portMUX_TYPE s_rejected_lock =
    portMUX_INITIALIZER_UNLOCKED;

static bt_spp_rejected_slot_t s_slots[
    BT_SPP_REJECTED_SLOT_COUNT
];

static uint32_t s_next_token =
    1;

static uint32_t next_token_locked(void)
{
    const uint32_t token =
        s_next_token;

    s_next_token +=
        1;

    if (s_next_token == 0) {
        s_next_token =
            1;
    }

    return token;
}

static int tracking_matches(
    uint32_t handle,
    uint32_t token
)
{
    int matches =
        0;

    portENTER_CRITICAL(
        &s_rejected_lock
    );

    for (
        size_t index = 0;
        index < BT_SPP_REJECTED_SLOT_COUNT;
        ++index
    ) {
        if (
            s_slots[index].active &&
            s_slots[index].handle ==
                handle &&
            s_slots[index].token ==
                token
        ) {
            matches =
                1;

            break;
        }
    }

    portEXIT_CRITICAL(
        &s_rejected_lock
    );

    return matches;
}

static void stop_tracking(
    uint32_t handle,
    uint32_t token
)
{
    portENTER_CRITICAL(
        &s_rejected_lock
    );

    for (
        size_t index = 0;
        index < BT_SPP_REJECTED_SLOT_COUNT;
        ++index
    ) {
        if (
            s_slots[index].active &&
            s_slots[index].handle ==
                handle &&
            (
                token == 0 ||
                s_slots[index].token ==
                    token
            )
        ) {
            memset(
                &s_slots[index],
                0,
                sizeof(s_slots[index])
            );
        }
    }

    portEXIT_CRITICAL(
        &s_rejected_lock
    );
}

static uint32_t begin_tracking(
    uint32_t handle
)
{
    uint32_t token =
        0;

    portENTER_CRITICAL(
        &s_rejected_lock
    );

    for (
        size_t index = 0;
        index < BT_SPP_REJECTED_SLOT_COUNT;
        ++index
    ) {
        if (
            s_slots[index].active &&
            s_slots[index].handle ==
                handle
        ) {
            s_slots[index].token =
                next_token_locked();

            token =
                s_slots[index].token;

            portEXIT_CRITICAL(
                &s_rejected_lock
            );

            return token;
        }
    }

    for (
        size_t index = 0;
        index < BT_SPP_REJECTED_SLOT_COUNT;
        ++index
    ) {
        if (!s_slots[index].active) {
            s_slots[index].handle =
                handle;

            s_slots[index].token =
                next_token_locked();

            s_slots[index].active =
                1;

            token =
                s_slots[index].token;

            break;
        }
    }

    portEXIT_CRITICAL(
        &s_rejected_lock
    );

    return token;
}

static int handle_became_authorized(
    uint32_t handle
)
{
    if (s_active_handle_callback == NULL) {
        return 0;
    }

    return
        handle != 0 &&
        s_active_handle_callback() ==
            handle;
}

static int queue_retry(
    uint32_t handle,
    uint32_t token,
    uint32_t attempt
)
{
    if (s_retry_queue == NULL) {
        return 0;
    }

    const bt_spp_rejected_work_t work = {
        .handle =
            handle,
        .token =
            token,
        .attempt =
            attempt
    };

    return xQueueSend(
        s_retry_queue,
        &work,
        0
    ) == pdTRUE;
}

static void rejected_client_retry_task(
    void *task_argument
)
{
    (void)task_argument;

    while (true) {
        bt_spp_rejected_work_t work = {
            0
        };

        if (
            xQueueReceive(
                s_retry_queue,
                &work,
                portMAX_DELAY
            ) != pdTRUE
        ) {
            continue;
        }

        vTaskDelay(
            pdMS_TO_TICKS(
                BT_SPP_REJECTED_RETRY_DELAY_MS
            )
        );

        if (
            !tracking_matches(
                work.handle,
                work.token
            )
        ) {
            continue;
        }

        if (
            handle_became_authorized(
                work.handle
            )
        ) {
            ESP_LOGW(
                TAG,
                "Canceling rejected-client retry because "
                "handle became active, handle=%" PRIu32,
                work.handle
            );

            stop_tracking(
                work.handle,
                work.token
            );

            continue;
        }

        const esp_err_t result =
            s_disconnect_callback(
                work.handle
            );

        if (result == ESP_OK) {
            ESP_LOGI(
                TAG,
                "Rejected-client disconnect retry accepted, "
                "handle=%" PRIu32 ", attempt=%" PRIu32,
                work.handle,
                work.attempt
            );

            continue;
        }

        if (
            work.attempt >=
                BT_SPP_REJECTED_MAXIMUM_ATTEMPTS
        ) {
            ESP_LOGE(
                TAG,
                "Rejected-client disconnect exhausted retries, "
                "handle=%" PRIu32 ", attempts=%" PRIu32
                ", error=%s",
                work.handle,
                work.attempt,
                esp_err_to_name(
                    result
                )
            );

            stop_tracking(
                work.handle,
                work.token
            );

            continue;
        }

        if (
            !queue_retry(
                work.handle,
                work.token,
                work.attempt + 1
            )
        ) {
            ESP_LOGE(
                TAG,
                "Rejected-client retry queue is full, "
                "handle=%" PRIu32,
                work.handle
            );

            stop_tracking(
                work.handle,
                work.token
            );
        }
    }
}

esp_err_t bt_spp_rejected_client_init(
    bt_spp_rejected_client_disconnect_t disconnect_callback,
    bt_spp_rejected_client_active_handle_t active_handle_callback
)
{
    if (
        disconnect_callback == NULL ||
        active_handle_callback == NULL
    ) {
        return ESP_ERR_INVALID_ARG;
    }

    if (
        s_retry_queue != NULL ||
        s_retry_task != NULL
    ) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(
        s_slots,
        0,
        sizeof(s_slots)
    );

    s_next_token =
        1;

    s_disconnect_callback =
        disconnect_callback;

    s_active_handle_callback =
        active_handle_callback;

    s_retry_queue =
        xQueueCreate(
            BT_SPP_REJECTED_QUEUE_LENGTH,
            sizeof(bt_spp_rejected_work_t)
        );

    if (s_retry_queue == NULL) {
        s_disconnect_callback =
            NULL;

        s_active_handle_callback =
            NULL;

        return ESP_ERR_NO_MEM;
    }

    const BaseType_t task_result =
        xTaskCreate(
            rejected_client_retry_task,
            "bt_spp_reject",
            BT_SPP_REJECTED_TASK_STACK_SIZE,
            NULL,
            BT_SPP_REJECTED_TASK_PRIORITY,
            &s_retry_task
        );

    if (task_result != pdPASS) {
        vQueueDelete(
            s_retry_queue
        );

        s_retry_queue =
            NULL;

        s_retry_task =
            NULL;

        s_disconnect_callback =
            NULL;

        s_active_handle_callback =
            NULL;

        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "Rejected-client disconnect recovery ready"
    );

    return ESP_OK;
}

void bt_spp_rejected_client_reject(
    uint32_t handle
)
{
    if (
        handle == 0 ||
        s_disconnect_callback == NULL ||
        s_active_handle_callback == NULL ||
        s_retry_queue == NULL
    ) {
        ESP_LOGE(
            TAG,
            "Cannot reject invalid handle or uninitialized client"
        );

        return;
    }

    if (
        handle_became_authorized(
            handle
        )
    ) {
        ESP_LOGW(
            TAG,
            "Refusing to disconnect active authorized handle, "
            "handle=%" PRIu32,
            handle
        );

        return;
    }

    const esp_err_t result =
        s_disconnect_callback(
            handle
        );

    if (result == ESP_OK) {
        return;
    }

    ESP_LOGE(
        TAG,
        "Initial rejected-client disconnect failed, "
        "handle=%" PRIu32 ", error=%s",
        handle,
        esp_err_to_name(
            result
        )
    );

    const uint32_t token =
        begin_tracking(
            handle
        );

    if (
        token == 0 ||
        !queue_retry(
            handle,
            token,
            2
        )
    ) {
        ESP_LOGE(
            TAG,
            "Unable to schedule rejected-client disconnect retry, "
            "handle=%" PRIu32,
            handle
        );

        stop_tracking(
            handle,
            token
        );
    }
}

void bt_spp_rejected_client_on_closed(
    uint32_t handle
)
{
    if (handle == 0) {
        return;
    }

    stop_tracking(
        handle,
        0
    );
}
