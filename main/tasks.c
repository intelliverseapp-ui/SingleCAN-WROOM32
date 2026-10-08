#include "tasks.h"

#include "driver/twai.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "singlecan_can.h"
#include "singlecan_leds.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

static const char *TAG_TASKS =
    "TASKS";

#define CAN_RX_TASK_STACK_SIZE 4096
#define CAN_RX_TASK_PRIORITY 5

#define CAN_HEALTH_TASK_STACK_SIZE 4096
#define CAN_HEALTH_TASK_PRIORITY 6

#define CAN_RX_IDLE_DELAY_MS 10
#define CAN_ERROR_DELAY_MS 250

#define SINGLECAN_TWAI_ALERTS \
    (TWAI_ALERT_BUS_OFF | \
     TWAI_ALERT_BUS_RECOVERED | \
     TWAI_ALERT_ERR_PASS | \
     TWAI_ALERT_ERR_ACTIVE | \
     TWAI_ALERT_ABOVE_ERR_WARN | \
     TWAI_ALERT_BELOW_ERR_WARN | \
     TWAI_ALERT_BUS_ERROR | \
     TWAI_ALERT_RX_QUEUE_FULL)

static TaskHandle_t s_can_rx_task_handle =
    NULL;

static TaskHandle_t s_can_health_task_handle =
    NULL;

static volatile bool s_bus_recovery_active =
    false;

// ------------------------------------------------------------
// LOG CURRENT TWAI HEALTH STATUS
// ------------------------------------------------------------

static void log_twai_status(
    const char *context
)
{
    twai_status_info_t status_info;

    const esp_err_t status_result =
        twai_get_status_info(
            &status_info
        );

    if (
        status_result !=
        ESP_OK
    ) {
        ESP_LOGW(
            TAG_TASKS,
            "Unable to read TWAI status during %s: %s",
            context,
            esp_err_to_name(
                status_result
            )
        );

        return;
    }

    /*
     * These are controller-health counters only.
     *
     * No raw CAN identifiers, DLC values, or payload bytes are
     * included in this diagnostic output.
     */
    ESP_LOGI(
        TAG_TASKS,
        "TWAI health during %s: "
        "state=%d "
        "tx_pending=%" PRIu32 " "
        "rx_pending=%" PRIu32 " "
        "tx_failed=%" PRIu32 " "
        "rx_missed=%" PRIu32 " "
        "rx_overrun=%" PRIu32 " "
        "bus_errors=%" PRIu32 " "
        "arbitration_lost=%" PRIu32,
        context,
        status_info.state,
        status_info.msgs_to_tx,
        status_info.msgs_to_rx,
        status_info.tx_failed_count,
        status_info.rx_missed_count,
        status_info.rx_overrun_count,
        status_info.bus_error_count,
        status_info.arb_lost_count
    );
}

// ------------------------------------------------------------
// INITIATE CONTROLLED BUS-OFF RECOVERY
// ------------------------------------------------------------

static void initiate_bus_recovery(void)
{
    if (s_bus_recovery_active) {
        ESP_LOGW(
            TAG_TASKS,
            "TWAI bus recovery is already active"
        );

        return;
    }

    s_bus_recovery_active =
        true;

    singlecan_leds_error();

    log_twai_status(
        "bus-off detection"
    );

    const esp_err_t recovery_result =
        twai_initiate_recovery();

    if (
        recovery_result !=
        ESP_OK
    ) {
        ESP_LOGE(
            TAG_TASKS,
            "TWAI recovery initiation failed: %s",
            esp_err_to_name(
                recovery_result
            )
        );

        s_bus_recovery_active =
            false;

        return;
    }

    ESP_LOGW(
        TAG_TASKS,
        "TWAI bus-off recovery initiated"
    );
}

// ------------------------------------------------------------
// COMPLETE RECOVERY AND RESTART TWAI
// ------------------------------------------------------------

static void complete_bus_recovery(void)
{
    ESP_LOGI(
        TAG_TASKS,
        "TWAI bus recovery completed"
    );

    /*
     * After recovery completes, the legacy TWAI driver remains in
     * the stopped state. Restart it before reception or any future
     * verified transmission resumes.
     */
    const esp_err_t start_result =
        twai_start();

    if (
        start_result !=
        ESP_OK
    ) {
        ESP_LOGE(
            TAG_TASKS,
            "TWAI restart after recovery failed: %s",
            esp_err_to_name(
                start_result
            )
        );

        singlecan_leds_error();

        /*
         * Leave recovery marked active. The RX drain task must not
         * treat a stopped controller as healthy.
         */
        return;
    }

    s_bus_recovery_active =
        false;

    ESP_LOGI(
        TAG_TASKS,
        "TWAI restarted after bus-off recovery"
    );

    log_twai_status(
        "successful recovery"
    );
}

// ------------------------------------------------------------
// PROCESS TWAI HEALTH ALERTS
// ------------------------------------------------------------

static void process_twai_alerts(
    uint32_t alerts
)
{
    if (
        alerts &
        TWAI_ALERT_BUS_OFF
    ) {
        ESP_LOGE(
            TAG_TASKS,
            "TWAI entered bus-off state"
        );

        initiate_bus_recovery();
    }

    if (
        alerts &
        TWAI_ALERT_BUS_RECOVERED
    ) {
        complete_bus_recovery();
    }

    if (
        alerts &
        TWAI_ALERT_ERR_PASS
    ) {
        ESP_LOGW(
            TAG_TASKS,
            "TWAI entered error-passive state"
        );

        log_twai_status(
            "error-passive alert"
        );
    }

    if (
        alerts &
        TWAI_ALERT_ERR_ACTIVE
    ) {
        ESP_LOGI(
            TAG_TASKS,
            "TWAI returned to error-active state"
        );
    }

    if (
        alerts &
        TWAI_ALERT_ABOVE_ERR_WARN
    ) {
        ESP_LOGW(
            TAG_TASKS,
            "TWAI exceeded the error-warning limit"
        );

        log_twai_status(
            "error-warning alert"
        );
    }

    if (
        alerts &
        TWAI_ALERT_BELOW_ERR_WARN
    ) {
        ESP_LOGI(
            TAG_TASKS,
            "TWAI dropped below the error-warning limit"
        );
    }

    if (
        alerts &
        TWAI_ALERT_BUS_ERROR
    ) {
        ESP_LOGW(
            TAG_TASKS,
            "TWAI bus error detected"
        );
    }

    if (
        alerts &
        TWAI_ALERT_RX_QUEUE_FULL
    ) {
        ESP_LOGE(
            TAG_TASKS,
            "TWAI receive queue is full"
        );

        singlecan_leds_error();

        log_twai_status(
            "receive-queue-full alert"
        );
    }
}

// ------------------------------------------------------------
// TWAI HEALTH AND RECOVERY TASK
// ------------------------------------------------------------

static void can_health_task(
    void *argument
)
{
    (void)argument;

    ESP_LOGI(
        TAG_TASKS,
        "TWAI health-monitor task started"
    );

    uint32_t previously_enabled_alerts =
        0;

    const esp_err_t alert_result =
        twai_reconfigure_alerts(
            SINGLECAN_TWAI_ALERTS,
            &previously_enabled_alerts
        );

    if (
        alert_result !=
        ESP_OK
    ) {
        ESP_LOGE(
            TAG_TASKS,
            "TWAI alert configuration failed: %s",
            esp_err_to_name(
                alert_result
            )
        );

        singlecan_leds_error();

        s_can_health_task_handle =
            NULL;

        vTaskDelete(
            NULL
        );

        return;
    }

    ESP_LOGI(
        TAG_TASKS,
        "TWAI health alerts enabled"
    );

    while (true) {
        uint32_t alerts =
            0;

        const esp_err_t read_result =
            twai_read_alerts(
                &alerts,
                portMAX_DELAY
            );

        if (
            read_result ==
            ESP_OK
        ) {
            process_twai_alerts(
                alerts
            );

            continue;
        }

        if (
            read_result ==
            ESP_ERR_INVALID_STATE
        ) {
            ESP_LOGW(
                TAG_TASKS,
                "TWAI alert monitor waiting for active driver"
            );
        } else {
            ESP_LOGE(
                TAG_TASKS,
                "TWAI alert read failed: %s",
                esp_err_to_name(
                    read_result
                )
            );

            singlecan_leds_error();
        }

        vTaskDelay(
            pdMS_TO_TICKS(
                CAN_ERROR_DELAY_MS
            )
        );
    }
}

// ------------------------------------------------------------
// SILENT TWAI RECEIVE-DRAIN TASK
// ------------------------------------------------------------

static void can_rx_drain_task(
    void *argument
)
{
    (void)argument;

    ESP_LOGI(
        TAG_TASKS,
        "TWAI receive-drain task started"
    );

    twai_message_t message;

    while (true) {
        if (s_bus_recovery_active) {
            vTaskDelay(
                pdMS_TO_TICKS(
                    CAN_ERROR_DELAY_MS
                )
            );

            continue;
        }

        const esp_err_t receive_result =
            singlecan_receive(
                &message
            );

        if (
            receive_result ==
            ESP_ERR_TIMEOUT
        ) {
            vTaskDelay(
                pdMS_TO_TICKS(
                    CAN_RX_IDLE_DELAY_MS
                )
            );

            continue;
        }

        if (
            receive_result ==
                ESP_ERR_INVALID_STATE &&
            s_bus_recovery_active
        ) {
            vTaskDelay(
                pdMS_TO_TICKS(
                    CAN_ERROR_DELAY_MS
                )
            );

            continue;
        }

        if (
            receive_result !=
            ESP_OK
        ) {
            ESP_LOGE(
                TAG_TASKS,
                "TWAI receive-drain error: %s",
                esp_err_to_name(
                    receive_result
                )
            );

            vTaskDelay(
                pdMS_TO_TICKS(
                    CAN_ERROR_DELAY_MS
                )
            );

            continue;
        }

        /*
         * The frame has been removed from the TWAI receive queue.
         *
         * PCAN hardware and PCAN-Explorer 7 own vehicle-bus capture
         * and decoding. SingleCAN does not log, serialize, queue, or
         * transmit this raw received frame over Bluetooth.
         */
    }
}

// ------------------------------------------------------------
// START TWAI TASKS
// ------------------------------------------------------------

void start_can_rx_task(void)
{
    if (
        s_can_rx_task_handle !=
            NULL ||
        s_can_health_task_handle !=
            NULL
    ) {
        ESP_LOGW(
            TAG_TASKS,
            "TWAI tasks are already running"
        );

        return;
    }

    const BaseType_t health_task_result =
        xTaskCreate(
            can_health_task,
            "can_health",
            CAN_HEALTH_TASK_STACK_SIZE,
            NULL,
            CAN_HEALTH_TASK_PRIORITY,
            &s_can_health_task_handle
        );

    if (
        health_task_result !=
        pdPASS
    ) {
        ESP_LOGE(
            TAG_TASKS,
            "TWAI health task creation failed"
        );

        s_can_health_task_handle =
            NULL;

        led_set_red();

        return;
    }

    const BaseType_t receive_task_result =
        xTaskCreate(
            can_rx_drain_task,
            "can_rx_drain",
            CAN_RX_TASK_STACK_SIZE,
            NULL,
            CAN_RX_TASK_PRIORITY,
            &s_can_rx_task_handle
        );

    if (
        receive_task_result !=
        pdPASS
    ) {
        ESP_LOGE(
            TAG_TASKS,
            "TWAI receive-drain task creation failed"
        );

        if (
            s_can_health_task_handle !=
            NULL
        ) {
            vTaskDelete(
                s_can_health_task_handle
            );

            s_can_health_task_handle =
                NULL;
        }

        s_can_rx_task_handle =
            NULL;

        led_set_red();

        return;
    }

    ESP_LOGI(
        TAG_TASKS,
        "TWAI receive-drain and health-monitor tasks started"
    );
}