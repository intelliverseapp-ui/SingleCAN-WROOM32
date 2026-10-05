#include "tasks.h"

#include "esp_log.h"
#include "singlecan_can.h"
#include "tcp_server.h"
#include "singlecan_leds.h"

static const char *TAG_TASKS = "TASKS";

// ------------------------------------------------------------
// CAN RX FORWARD TASK
// ------------------------------------------------------------
static void can_rx_forward_task(void *arg)
{
    ESP_LOGI(TAG_TASKS, "CAN RX forward task started");

    twai_message_t msg;

    while (1) {
        esp_err_t ret = singlecan_receive(&msg);

        if (ret == ESP_ERR_TIMEOUT) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (ret != ESP_OK) {
            ESP_LOGE(TAG_TASKS, "CAN RX error: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        ESP_LOGI(TAG_TASKS,
                 "CAN_RX %lu DLC=%u DATA=%02X %02X %02X %02X %02X %02X %02X %02X",
                 (unsigned long)msg.identifier,
                 msg.data_length_code,
                 msg.data[0], msg.data[1], msg.data[2], msg.data[3],
                 msg.data[4], msg.data[5], msg.data[6], msg.data[7]);
    }
}

// Public wrapper
void start_can_rx_task(void)
{
    BaseType_t ret = xTaskCreate(
        can_rx_forward_task,
        "can_rx_forward",
        4096,
        NULL,
        5,
        NULL
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG_TASKS, "CAN RX task creation FAILED");
        led_set_red();
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ------------------------------------------------------------
// TCP SERVER TASK
// ------------------------------------------------------------
static void tcp_server_task_wrapper(void *arg)
{
    tcp_server_task(NULL);   // FIXED: pass NULL as required by signature
}

void start_tcp_server_task(void)
{
    BaseType_t ret = xTaskCreate(
        tcp_server_task_wrapper,
        "tcp_server",
        4096,
        NULL,
        10,
        NULL
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG_TASKS, "TCP server task creation FAILED");
        led_set_red();
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
