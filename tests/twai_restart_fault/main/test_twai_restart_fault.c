#include "tasks.h"

#include "driver/twai.h"
#include "esp_err.h"
#include "unity.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>
#include <string.h>

static volatile uint32_t s_pending_alerts =
    0;

static volatile uint32_t s_recovery_count =
    0;

static volatile uint32_t s_start_count =
    0;

static volatile uint32_t s_error_led_count =
    0;

esp_err_t __wrap_twai_reconfigure_alerts(
    uint32_t alerts_enabled,
    uint32_t *current_alerts
)
{
    TEST_ASSERT_NOT_EQUAL(
        0,
        alerts_enabled
    );

    if (current_alerts != NULL) {
        *current_alerts =
            0;
    }

    return ESP_OK;
}

esp_err_t __wrap_twai_read_alerts(
    uint32_t *alerts,
    TickType_t ticks_to_wait
)
{
    if (ticks_to_wait > 0) {
        vTaskDelay(
            ticks_to_wait
        );
    }

    if (
        alerts != NULL &&
        s_pending_alerts != 0
    ) {
        *alerts =
            s_pending_alerts;

        s_pending_alerts =
            0;

        return ESP_OK;
    }

    if (alerts != NULL) {
        *alerts =
            0;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t __wrap_twai_get_status_info(
    twai_status_info_t *status_info
)
{
    if (status_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(
        status_info,
        0,
        sizeof(*status_info)
    );

    return ESP_OK;
}

esp_err_t __wrap_twai_initiate_recovery(void)
{
    s_recovery_count +=
        1;

    return ESP_OK;
}

esp_err_t __wrap_twai_start(void)
{
    s_start_count +=
        1;

    return ESP_FAIL;
}

esp_err_t singlecan_receive(
    twai_message_t *message
)
{
    if (message != NULL) {
        memset(
            message,
            0,
            sizeof(*message)
        );
    }

    return ESP_ERR_TIMEOUT;
}

void singlecan_leds_error(void)
{
    s_error_led_count +=
        1;
}

static void test_restart_exhaustion_enters_terminal_fault(void)
{
    TEST_ASSERT_EQUAL(
        ESP_OK,
        start_can_rx_task()
    );

    TEST_ASSERT_TRUE(
        singlecan_tasks_is_twai_running()
    );

    s_pending_alerts =
        TWAI_ALERT_BUS_OFF;

    const TickType_t recovery_deadline =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(
            2000
        );

    while (
        s_recovery_count == 0 &&
        xTaskGetTickCount() < recovery_deadline
    ) {
        vTaskDelay(
            pdMS_TO_TICKS(
                10
            )
        );
    }

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_recovery_count
    );

    TEST_ASSERT_FALSE(
        singlecan_tasks_is_twai_running()
    );

    s_pending_alerts =
        TWAI_ALERT_BUS_RECOVERED;

    const TickType_t fault_deadline =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(
            4000
        );

    while (
        s_start_count < 3 &&
        xTaskGetTickCount() < fault_deadline
    ) {
        vTaskDelay(
            pdMS_TO_TICKS(
                10
            )
        );
    }

    TEST_ASSERT_EQUAL_UINT32(
        3,
        s_start_count
    );

    TEST_ASSERT_FALSE(
        singlecan_tasks_is_twai_running()
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_recovery_count
    );

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(
        4,
        s_error_led_count
    );

    const uint32_t starts_at_fault =
        s_start_count;

    vTaskDelay(
        pdMS_TO_TICKS(
            750
        )
    );

    TEST_ASSERT_FALSE(
        singlecan_tasks_is_twai_running()
    );

    TEST_ASSERT_EQUAL_UINT32(
        starts_at_fault,
        s_start_count
    );
}

void app_main(void)
{
    UNITY_BEGIN();

    RUN_TEST(
        test_restart_exhaustion_enters_terminal_fault
    );

    UNITY_END();
}
