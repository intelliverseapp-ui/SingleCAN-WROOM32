#include "tcp_queue.h"
#include "singlecan_leds.h"
#include "esp_log.h"
#include <string.h>

#include "esp_spp_api.h"
#include "bt_spp.h"

static const char *TAG = "TCP_QUEUE";

// Global queue handle
QueueHandle_t tcp_outbound_queue = NULL;

// ------------------------------------------------------------
// Push a line into the outbound queue (non-blocking)
// ------------------------------------------------------------
void tcp_queue_push(const char *line)
{
    if (!tcp_outbound_queue || !line) {
        ESP_LOGE(TAG, "Queue not ready or line NULL");
        return;
    }

    tcp_queue_item_t item;
    memset(&item, 0, sizeof(item));

    size_t len = strnlen(line, TCP_QUEUE_MAX_LINE_LEN - 1);
    memcpy(item.line, line, len);
    item.len = len;

    BaseType_t ok = xQueueSend(tcp_outbound_queue, &item, 0);

    if (ok != pdTRUE) {
        ESP_LOGW(TAG, "Outbound queue FULL — dropping line");
        singlecan_leds_error();
    }
}

// ------------------------------------------------------------
// Queue consumer task — drains queue and sends lines over SPP
// ------------------------------------------------------------
void tcp_queue_task(void *arg)
{
    tcp_queue_item_t item;

    ESP_LOGI(TAG, "Outbound queue task started");

    while (1) {
        if (xQueueReceive(tcp_outbound_queue, &item, portMAX_DELAY) == pdTRUE) {

            uint32_t handle = bt_spp_get_handle();
            int connected = bt_spp_is_connected();

            if (!connected || handle == 0) {
                ESP_LOGW(TAG,
                         "No SPP client connected — dropping outbound line: %.*s",
                         item.len, item.line);
                continue;
            }

            esp_err_t ret = esp_spp_write(handle,
                                          item.len,
                                          (uint8_t *)item.line);

            if (ret != ESP_OK) {
                ESP_LOGE(TAG,
                         "esp_spp_write FAILED (%s) — line dropped: %.*s",
                         esp_err_to_name(ret),
                         item.len,
                         item.line);
                singlecan_leds_error();
            } else {
                ESP_LOGI(TAG,
                         "Sent outbound line over SPP: %.*s",
                         item.len,
                         item.line);
            }
        }
    }
}

// ------------------------------------------------------------
// Initialization — create queue + start sender task
// ------------------------------------------------------------
void tcp_queue_init(void)
{
    ESP_LOGI(TAG, "Initializing outbound queue...");

    tcp_outbound_queue = xQueueCreate(TCP_QUEUE_LENGTH,
                                      sizeof(tcp_queue_item_t));

    if (!tcp_outbound_queue) {
        ESP_LOGE(TAG, "FAILED to create outbound queue");
        singlecan_leds_error();
        return;
    }

    BaseType_t ok = xTaskCreate(
        tcp_queue_task,
        "tcp_queue_task",
        4096,
        NULL,
        5,
        NULL
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "FAILED to start outbound queue task");
        singlecan_leds_error();
        return;
    }

    ESP_LOGI(TAG, "Outbound queue ready");
}
