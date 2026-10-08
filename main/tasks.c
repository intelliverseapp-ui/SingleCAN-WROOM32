#include "tasks.h"

#include "driver/twai.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
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

#define CAN_TASK_START_TIMEOUT_MS 5000

#define CAN_TASK_EVENT_HEALTH_READY BIT0
#define CAN_TASK_EVENT_RX_READY BIT1
#define CAN_TASK_EVENT_FAILED BIT2

#define CAN_TASK_EVENT_ALL_READY \
    (CAN_TASK_EVENT_HEALTH_READY | CAN_TASK_EVENT_RX_READY)

#define CAN_RECOVERY_MAXIMUM_ATTEMPTS 3
#define CAN_RECOVERY_RETRY_DELAY_MS 500

#define SINGLECAN_TWAI_ALERTS \
    (TWAI_ALERT_BUS_OFF | \
     TWAI_ALERT_BUS_RECOVERED | \
     TWAI_ALERT_ERR_PASS | \
     TWAI_ALERT_ERR_ACTIVE | \
     TWAI_ALERT_ABOVE_ERR_WARN | \
     TWAI_ALERT_BELOW_ERR_WARN | \
     TWAI_ALERT_BUS_ERROR | \
     TWAI_ALERT_RX_QUEUE_FULL)

typedef enum {
    TWAI_HEALTH_STARTING = 0,
    TWAI_HEALTH_RUNNING,
    TWAI_HEALTH_RECOVERING,
    TWAI_HEALTH_FAULTED
} singlecan_twai_health_t;

static TaskHandle_t s_can_rx_task_handle =
    NULL;

static TaskHandle_t s_can_health_task_handle =
    NULL;

static EventGroupHandle_t s_can_task_start_events =
    NULL;

static volatile singlecan_twai_health_t s_twai_health =
    TWAI_HEALTH_STARTING;

// ------------------------------------------------------------
// TWAI HEALTH-STATE HELPERS
// ------------------------------------------------------------

static const char *twai_health_name(
    singlecan_twai_health_t health
)
{
    switch (health) {
        case TWAI_HEALTH_STARTING:
            return "STARTING";

        case TWAI_HEALTH_RUNNING:
            return "RUNNING";

        case TWAI_HEALTH_RECOVERING:
            return "RECOVERING";

        case TWAI_HEALTH_FAULTED:
            return "FAULTED";

        default:
            return "UNKNOWN";
    }
}

static void set_twai_health(
    singlecan_twai_health_t health
)
{
    s_twai_health =
        health;

    ESP_LOGI(
        TAG_TASKS,
        "TWAI health state changed: %s",
        twai_health_name(
            health
        )
    );
}

static bool twai_is_running(void)
{
    return
        s_twai_health ==
        TWAI_HEALTH_RUNNING;
}

static bool twai_is_recovering(void)
{
    return
        s_twai_health ==
        TWAI_HEALTH_RECOVERING;
}

static bool twai_is_faulted(void)
{
    return
        s_twai_health ==
        TWAI_HEALTH_FAULTED;
}

static void enter_twai_fault_state(
    const char *reason
)
{
    set_twai_health(
        TWAI_HEALTH_FAULTED
    );

    singlecan_leds_error();

    ESP_LOGE(
        TAG_TASKS,
        "TWAI entered terminal fault state: %s",
        reason
    );

    /*
     * The receive-drain task will remain paused while faulted.
     *
     * Bluetooth command processing can continue, but no verified CAN
     * transmission should be considered available until the device
     * is deliberately restarted or a future supervised
     * reinitialization path is implemented.
     */
}

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
        "software_state=%s "
        "driver_state=%d "
        "tx_pending=%" PRIu32 " "
        "rx_pending=%" PRIu32 " "
        "tx_failed=%" PRIu32 " "
        "rx_missed=%" PRIu32 " "
        "rx_overrun=%" PRIu32 " "
        "bus_errors=%" PRIu32 " "
        "arbitration_lost=%" PRIu32,
        context,
        twai_health_name(
            s_twai_health
        ),
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
    if (twai_is_recovering()) {
        ESP_LOGW(
            TAG_TASKS,
            "TWAI bus recovery is already active"
        );

        return;
    }

    if (twai_is_faulted()) {
        ESP_LOGE(
            TAG_TASKS,
            "TWAI recovery rejected because controller is faulted"
        );

        return;
    }

    set_twai_health(
        TWAI_HEALTH_RECOVERING
    );

    singlecan_leds_error();

    log_twai_status(
        "bus-off detection"
    );

    for (
        uint32_t attempt = 1;
        attempt <=
            CAN_RECOVERY_MAXIMUM_ATTEMPTS;
        ++attempt
    ) {
        const esp_err_t recovery_result =
            twai_initiate_recovery();

        if (
            recovery_result ==
            ESP_OK
        ) {
            ESP_LOGW(
                TAG_TASKS,
                "TWAI bus-off recovery initiated "
                "on attempt %" PRIu32,
                attempt
            );

            return;
        }

        ESP_LOGE(
            TAG_TASKS,
            "TWAI recovery initiation attempt "
            "%" PRIu32 " failed: %s",
            attempt,
            esp_err_to_name(
                recovery_result
            )
        );

        if (
            attempt <
            CAN_RECOVERY_MAXIMUM_ATTEMPTS
        ) {
            vTaskDelay(
                pdMS_TO_TICKS(
                    CAN_RECOVERY_RETRY_DELAY_MS
                )
            );
        }
    }

    enter_twai_fault_state(
        "recovery initiation exhausted all attempts"
    );
}

// ------------------------------------------------------------
// COMPLETE RECOVERY AND RESTART TWAI
// ------------------------------------------------------------

static void complete_bus_recovery(void)
{
    if (twai_is_faulted()) {
        ESP_LOGE(
            TAG_TASKS,
            "TWAI recovery-complete alert ignored because " \
            "controller is faulted"
        );

        return;
    }

    if (!twai_is_recovering()) {
        ESP_LOGW(
            TAG_TASKS,
            "TWAI recovery-complete alert received without " \
            "an active recovery state"
        );
    }

    ESP_LOGI(
        TAG_TASKS,
        "TWAI bus recovery completed; restarting driver"
    );

    /*
     * After recovery completes, the legacy TWAI driver remains in
     * the stopped state. Restart it before reception or future
     * verified transmission resumes.
     */
    for (
        uint32_t attempt = 1;
        attempt <=
            CAN_RECOVERY_MAXIMUM_ATTEMPTS;
        ++attempt
    ) {
        const esp_err_t start_result =
            twai_start();

        if (
            start_result ==
            ESP_OK
        ) {
            set_twai_health(
                TWAI_HEALTH_RUNNING
            );

            ESP_LOGI(
                TAG_TASKS,
                "TWAI restarted after recovery " \
                "on attempt %" PRIu32,
                attempt
            );

            log_twai_status(
                "successful recovery"
            );

            return;
        }

        ESP_LOGE(
            TAG_TASKS,
            "TWAI restart attempt %" PRIu32
            " after recovery failed: %s",
            attempt,
            esp_err_to_name(
                start_result
            )
        );

        singlecan_leds_error();

        if (
            attempt <
            CAN_RECOVERY_MAXIMUM_ATTEMPTS
        ) {
            vTaskDelay(
                pdMS_TO_TICKS(
                    CAN_RECOVERY_RETRY_DELAY_MS
                )
            );
        }
    }

    enter_twai_fault_state(
        "TWAI restart exhausted all attempts"
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

        enter_twai_fault_state(
            "TWAI alert configuration failed"
        );

        if (
            s_can_task_start_events !=
            NULL
        ) {
            xEventGroupSetBits(
                s_can_task_start_events,
                CAN_TASK_EVENT_FAILED
            );
        }

        s_can_health_task_handle =
            NULL;

        vTaskDelete(
            NULL
        );

        return;
    }

    set_twai_health(
        TWAI_HEALTH_RUNNING
    );

    ESP_LOGI(
        TAG_TASKS,
        "TWAI health alerts enabled"
    );

    if (
        s_can_task_start_events !=
        NULL
    ) {
        xEventGroupSetBits(
            s_can_task_start_events,
            CAN_TASK_EVENT_HEALTH_READY
        );
    }

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
            if (twai_is_recovering()) {
                ESP_LOGW(
                    TAG_TASKS,
                    "TWAI alert monitor waiting during recovery"
                );
            } else if (twai_is_faulted()) {
                ESP_LOGE(
                    TAG_TASKS,
                    "TWAI alert monitor stopped by terminal fault"
                );

                vTaskDelay(
                    pdMS_TO_TICKS(
                        CAN_ERROR_DELAY_MS
                    )
                );
            } else {
                ESP_LOGE(
                    TAG_TASKS,
                    "TWAI alert monitor lost active driver state"
                );

                enter_twai_fault_state(
                    "TWAI alert driver became unavailable"
                );
            }
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

    if (
        s_can_task_start_events !=
        NULL
    ) {
        xEventGroupSetBits(
            s_can_task_start_events,
            CAN_TASK_EVENT_RX_READY
        );
    }

    twai_message_t message;

    while (true) {
        if (!twai_is_running()) {
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
            !twai_is_running()
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
         * transmit this raw received frame through Bluetooth.
         */
    }
}

// ------------------------------------------------------------
// START TWAI TASKS
// ------------------------------------------------------------

esp_err_t start_can_rx_task(void)
{
    if (
        s_can_rx_task_handle != NULL ||
        s_can_health_task_handle != NULL
    ) {
        ESP_LOGW(
            TAG_TASKS,
            "TWAI tasks are already running"
        );

        return ESP_ERR_INVALID_STATE;
    }

    if (s_can_task_start_events == NULL) {
        s_can_task_start_events =
            xEventGroupCreate();

        if (s_can_task_start_events == NULL) {
            ESP_LOGE(
                TAG_TASKS,
                "TWAI startup event-group creation failed"
            );

            enter_twai_fault_state(
                "TWAI startup synchronization creation failed"
            );

            return ESP_ERR_NO_MEM;
        }
    }

    xEventGroupClearBits(
        s_can_task_start_events,
        CAN_TASK_EVENT_ALL_READY |
            CAN_TASK_EVENT_FAILED
    );

    set_twai_health(
        TWAI_HEALTH_STARTING
    );

    const BaseType_t health_task_result =
        xTaskCreate(
            can_health_task,
            "can_health",
            CAN_HEALTH_TASK_STACK_SIZE,
            NULL,
            CAN_HEALTH_TASK_PRIORITY,
            &s_can_health_task_handle
        );

    if (health_task_result != pdPASS) {
        ESP_LOGE(
            TAG_TASKS,
            "TWAI health task creation failed"
        );

        s_can_health_task_handle =
            NULL;

        enter_twai_fault_state(
            "TWAI health task creation failed"
        );

        return ESP_ERR_NO_MEM;
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

    if (receive_task_result != pdPASS) {
        ESP_LOGE(
            TAG_TASKS,
            "TWAI receive-drain task creation failed"
        );

        if (s_can_health_task_handle != NULL) {
            vTaskDelete(
                s_can_health_task_handle
            );

            s_can_health_task_handle =
                NULL;
        }

        s_can_rx_task_handle =
            NULL;

        enter_twai_fault_state(
            "TWAI receive-drain task creation failed"
        );

        return ESP_ERR_NO_MEM;
    }

    const EventBits_t startup_bits =
        xEventGroupWaitBits(
            s_can_task_start_events,
            CAN_TASK_EVENT_ALL_READY |
                CAN_TASK_EVENT_FAILED,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(
                CAN_TASK_START_TIMEOUT_MS
            )
        );

    if (
        startup_bits &
        CAN_TASK_EVENT_FAILED
    ) {
        ESP_LOGE(
            TAG_TASKS,
            "TWAI task startup reported an asynchronous failure"
        );

        enter_twai_fault_state(
            "TWAI task asynchronous startup failure"
        );

        return ESP_FAIL;
    }

    if (
        (
            startup_bits &
            CAN_TASK_EVENT_ALL_READY
        ) !=
        CAN_TASK_EVENT_ALL_READY
    ) {
        ESP_LOGE(
            TAG_TASKS,
            "Timed out waiting for TWAI task readiness"
        );

        enter_twai_fault_state(
            "TWAI task readiness timeout"
        );

        return ESP_ERR_TIMEOUT;
    }

    if (!twai_is_running()) {
        ESP_LOGE(
            TAG_TASKS,
            "TWAI tasks reported ready without RUNNING health"
        );

        enter_twai_fault_state(
            "TWAI readiness verification failed"
        );

        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(
        TAG_TASKS,
        "TWAI receive-drain and health-monitor tasks ready"
    );

    return ESP_OK;
}
