#include "singlecan_can.h"

#include "driver/twai.h"
#include "esp_err.h"
#include "unity.h"

#include "freertos/FreeRTOS.h"

#include <stdbool.h>
#include <stdint.h>

static volatile bool s_twai_running =
    false;

static volatile uint32_t s_transmit_count =
    0;

static volatile uint32_t s_error_led_count =
    0;

static volatile uint32_t s_tx_led_count =
    0;

static void reset_observations(void)
{
    s_transmit_count =
        0;

    s_error_led_count =
        0;

    s_tx_led_count =
        0;
}

bool singlecan_tasks_is_twai_running(void)
{
    return s_twai_running;
}

esp_err_t __wrap_twai_transmit(
    const twai_message_t *message,
    TickType_t ticks_to_wait
)
{
    (void)message;
    (void)ticks_to_wait;

    s_transmit_count +=
        1;

    return ESP_OK;
}

void singlecan_leds_error(void)
{
    s_error_led_count +=
        1;
}

void singlecan_leds_can_tx_active(void)
{
    s_tx_led_count +=
        1;
}

void singlecan_leds_can_rx_active(void)
{
}

static void test_disabled_can_rejects_before_transmit(void)
{
    reset_observations();

    s_twai_running =
        true;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        singlecan_disable_can()
    );

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        singlecan_send_verified(
            SINGLECAN_VERIFIED_COMMAND_NONE
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_transmit_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_tx_led_count
    );
}

static void test_nonrunning_supervisor_rejects_before_transmit(void)
{
    reset_observations();

    TEST_ASSERT_EQUAL(
        ESP_OK,
        singlecan_enable_can()
    );

    s_twai_running =
        false;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        singlecan_send_verified(
            SINGLECAN_VERIFIED_COMMAND_NONE
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_transmit_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_tx_led_count
    );
}

static void test_missing_verified_mapping_never_reaches_transmit(void)
{
    reset_observations();

    TEST_ASSERT_EQUAL(
        ESP_OK,
        singlecan_enable_can()
    );

    s_twai_running =
        true;

    TEST_ASSERT_EQUAL(
        ESP_ERR_NOT_SUPPORTED,
        singlecan_send_verified(
            SINGLECAN_VERIFIED_COMMAND_NONE
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_transmit_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_error_led_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_tx_led_count
    );

    TEST_ASSERT_EQUAL(
        ESP_ERR_NOT_SUPPORTED,
        singlecan_send_verified(
            (singlecan_verified_command_t)1
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_transmit_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_tx_led_count
    );
}

void app_main(void)
{
    UNITY_BEGIN();

    RUN_TEST(
        test_disabled_can_rejects_before_transmit
    );

    RUN_TEST(
        test_nonrunning_supervisor_rejects_before_transmit
    );

    RUN_TEST(
        test_missing_verified_mapping_never_reaches_transmit
    );

    UNITY_END();
}
