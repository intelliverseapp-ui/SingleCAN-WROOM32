#include "tasks.h"

#include "driver/twai.h"
#include "esp_err.h"
#include "unity.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static volatile uint32_t s_alert_configuration_count =
    0;

static volatile uint32_t s_alert_read_count =
    0;

static volatile uint32_t s_receive_count =
    0;

static volatile uint32_t s_error_led_count =
    0;

static volatile uint32_t s_recovery_count =
    0;

static volatile uint32_t s_start_count =
    0;

static volatile esp_err_t s_alert_configuration_result =
    ESP_OK;

static volatile esp_err_t s_alert_read_result =
    ESP_ERR_TIMEOUT;

static volatile uint32_t s_next_alerts =
    0;

static volatile esp_err_t s_recovery_result =
    ESP_OK;

static volatile esp_err_t s_start_result =
    ESP_OK;

static void reset_test_observations(void)
{
    s_alert_configuration_count =
        0;

    s_alert_read_count =
        0;

    s_receive_count =
        0;

    s_error_led_count =
        0;

    s_recovery_count =
        0;

    s_start_count =
        0;

    s_alert_configuration_result =
        ESP_OK;

    s_alert_read_result =
        ESP_ERR_TIMEOUT;

    s_next_alerts =
        0;

    s_recovery_result =
        ESP_OK;

    s_start_result =
        ESP_OK;
}

esp_err_t __wrap_twai_reconfigure_alerts(
    uint32_t alerts_enabled,
    uint32_t *current_alerts
)
{
    s_alert_configuration_count +=
        1;

    if (current_alerts != NULL) {
        *current_alerts =
            0;
    }

    TEST_ASSERT_NOT_EQUAL(
        0,
        alerts_enabled
    );

    return s_alert_configuration_result;
}

esp_err_t __wrap_twai_read_alerts(
    uint32_t *alerts,
    TickType_t ticks_to_wait
)
{
    s_alert_read_count +=
        1;

    if (
        alerts != NULL &&
        s_alert_read_result == ESP_OK
    ) {
        *alerts =
            s_next_alerts;

        s_next_alerts =
            0;

        s_alert_read_result =
            ESP_ERR_TIMEOUT;

        return ESP_OK;
    }

    if (ticks_to_wait > 0) {
        vTaskDelay(
            ticks_to_wait
        );
    }

    return s_alert_read_result;
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

    return s_recovery_result;
}

esp_err_t __wrap_twai_start(void)
{
    s_start_count +=
        1;

    return s_start_result;
}

esp_err_t singlecan_receive(
    twai_message_t *message
)
{
    s_receive_count +=
        1;

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

static void test_supervisor_starts_and_reports_running(void)
{
    reset_test_observations();

    TEST_ASSERT_EQUAL(
        ESP_OK,
        start_can_rx_task()
    );

    TEST_ASSERT_TRUE(
        singlecan_tasks_is_twai_running()
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_alert_configuration_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_error_led_count
    );

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        start_can_rx_task()
    );

    vTaskDelay(
        pdMS_TO_TICKS(
            300
        )
    );

    TEST_ASSERT_GREATER_THAN_UINT32(
        0,
        s_alert_read_count
    );

    TEST_ASSERT_GREATER_THAN_UINT32(
        0,
        s_receive_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_recovery_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_start_count
    );
}


static void test_bus_off_recovery_returns_supervisor_to_running(void)
{
    reset_test_observations();

    TEST_ASSERT_TRUE(
        singlecan_tasks_is_twai_running()
    );

    s_next_alerts =
        TWAI_ALERT_BUS_OFF;

    s_alert_read_result =
        ESP_OK;

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

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_error_led_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_start_count
    );

    s_next_alerts =
        TWAI_ALERT_BUS_RECOVERED;

    s_alert_read_result =
        ESP_OK;

    const TickType_t restart_deadline =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(
            2000
        );

    while (
        s_start_count == 0 &&
        xTaskGetTickCount() < restart_deadline
    ) {
        vTaskDelay(
            pdMS_TO_TICKS(
                10
            )
        );
    }

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_start_count
    );

    TEST_ASSERT_TRUE(
        singlecan_tasks_is_twai_running()
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_recovery_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_error_led_count
    );
}


static void test_recovery_initiation_exhaustion_enters_terminal_fault(void)
{
    reset_test_observations();

    TEST_ASSERT_TRUE(
        singlecan_tasks_is_twai_running()
    );

    s_recovery_result =
        ESP_FAIL;

    s_next_alerts =
        TWAI_ALERT_BUS_OFF;

    s_alert_read_result =
        ESP_OK;

    const TickType_t fault_deadline =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(
            4000
        );

    while (
        s_recovery_count < 3 &&
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
        s_recovery_count
    );

    const TickType_t state_deadline =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(
            1000
        );

    while (
        singlecan_tasks_is_twai_running() &&
        xTaskGetTickCount() < state_deadline
    ) {
        vTaskDelay(
            pdMS_TO_TICKS(
                10
            )
        );
    }

    TEST_ASSERT_FALSE(
        singlecan_tasks_is_twai_running()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_start_count
    );

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(
        2,
        s_error_led_count
    );

    s_next_alerts =
        TWAI_ALERT_BUS_RECOVERED;

    s_alert_read_result =
        ESP_OK;

    vTaskDelay(
        pdMS_TO_TICKS(
            400
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_start_count
    );

    TEST_ASSERT_FALSE(
        singlecan_tasks_is_twai_running()
    );
}

void app_main(void)
{
    UNITY_BEGIN();

    RUN_TEST(
        test_supervisor_starts_and_reports_running
    );


    RUN_TEST(
        test_bus_off_recovery_returns_supervisor_to_running
    );


    RUN_TEST(
        test_recovery_initiation_exhaustion_enters_terminal_fault
    );

    UNITY_END();
}
