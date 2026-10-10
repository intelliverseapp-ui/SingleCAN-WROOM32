#include "bt_spp_rejected_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "unity.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TEST_MAX_DISCONNECT_CALLS 32
#define TEST_WAIT_STEP_MS 10
#define TEST_RETRY_SETTLE_MS 900

static portMUX_TYPE s_test_lock =
    portMUX_INITIALIZER_UNLOCKED;

static uint32_t s_active_handle =
    0;

static uint32_t s_disconnect_count =
    0;

static uint32_t s_disconnect_handles[
    TEST_MAX_DISCONNECT_CALLS
];

static uint32_t s_failures_before_success =
    0;

static int s_always_fail =
    0;

static void reset_observation(void)
{
    portENTER_CRITICAL(
        &s_test_lock
    );

    s_active_handle =
        0;

    s_disconnect_count =
        0;

    memset(
        s_disconnect_handles,
        0,
        sizeof(s_disconnect_handles)
    );

    s_failures_before_success =
        0;

    s_always_fail =
        0;

    portEXIT_CRITICAL(
        &s_test_lock
    );
}

static uint32_t observed_disconnect_count(void)
{
    uint32_t count;

    portENTER_CRITICAL(
        &s_test_lock
    );

    count =
        s_disconnect_count;

    portEXIT_CRITICAL(
        &s_test_lock
    );

    return count;
}

static uint32_t observed_disconnect_handle(
    size_t index
)
{
    uint32_t handle =
        0;

    portENTER_CRITICAL(
        &s_test_lock
    );

    if (
        index <
            TEST_MAX_DISCONNECT_CALLS
    ) {
        handle =
            s_disconnect_handles[index];
    }

    portEXIT_CRITICAL(
        &s_test_lock
    );

    return handle;
}

static void set_active_handle(
    uint32_t handle
)
{
    portENTER_CRITICAL(
        &s_test_lock
    );

    s_active_handle =
        handle;

    portEXIT_CRITICAL(
        &s_test_lock
    );
}

static void set_disconnect_behavior(
    uint32_t failures_before_success,
    int always_fail
)
{
    portENTER_CRITICAL(
        &s_test_lock
    );

    s_failures_before_success =
        failures_before_success;

    s_always_fail =
        always_fail;

    portEXIT_CRITICAL(
        &s_test_lock
    );
}

static int observe_disconnect(
    uint32_t handle,
    esp_err_t *result
)
{
    if (result == NULL) {
        return 0;
    }

    *result =
        ESP_OK;

    portENTER_CRITICAL(
        &s_test_lock
    );

    const uint32_t call_index =
        s_disconnect_count;

    if (
        call_index <
            TEST_MAX_DISCONNECT_CALLS
    ) {
        s_disconnect_handles[call_index] =
            handle;
    }

    s_disconnect_count +=
        1;

    if (
        s_always_fail ||
        s_disconnect_count <=
            s_failures_before_success
    ) {
        *result =
            ESP_FAIL;
    }

    portEXIT_CRITICAL(
        &s_test_lock
    );

    return 1;
}

static uint32_t provide_active_handle(void)
{
    uint32_t handle;

    portENTER_CRITICAL(
        &s_test_lock
    );

    handle =
        s_active_handle;

    portEXIT_CRITICAL(
        &s_test_lock
    );

    return handle;
}

static int observe_guarded_disconnect(
    uint32_t handle,
    esp_err_t *result
)
{
    if (
        result == NULL ||
        handle ==
            provide_active_handle()
    ) {
        return 0;
    }

    return observe_disconnect(
        handle,
        result
    );
}

static int wait_for_disconnect_count(
    uint32_t expected_count,
    uint32_t timeout_ms
)
{
    const TickType_t deadline =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(
            timeout_ms
        );

    while (
        observed_disconnect_count() <
            expected_count &&
        xTaskGetTickCount() <
            deadline
    ) {
        vTaskDelay(
            pdMS_TO_TICKS(
                TEST_WAIT_STEP_MS
            )
        );
    }

    return
        observed_disconnect_count() >=
            expected_count;
}

static void settle_retry_worker(void)
{
    vTaskDelay(
        pdMS_TO_TICKS(
            TEST_RETRY_SETTLE_MS
        )
    );
}

static void test_immediate_disconnect_success_needs_no_retry(void)
{
    reset_observation();

    bt_spp_rejected_client_reject(
        100
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        observed_disconnect_count()
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        observed_disconnect_handle(
            0
        )
    );

    settle_retry_worker();

    TEST_ASSERT_EQUAL_UINT32(
        1,
        observed_disconnect_count()
    );

    bt_spp_rejected_client_on_closed(
        100
    );
}

static void test_failed_disconnect_retries_and_succeeds(void)
{
    reset_observation();

    set_disconnect_behavior(
        1,
        0
    );

    bt_spp_rejected_client_reject(
        200
    );

    TEST_ASSERT_TRUE(
        wait_for_disconnect_count(
            2,
            1000
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        2,
        observed_disconnect_count()
    );

    TEST_ASSERT_EQUAL_UINT32(
        200,
        observed_disconnect_handle(
            0
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        200,
        observed_disconnect_handle(
            1
        )
    );

    bt_spp_rejected_client_on_closed(
        200
    );
}

static void test_disconnect_retry_exhaustion_is_bounded(void)
{
    reset_observation();

    set_disconnect_behavior(
        0,
        1
    );

    bt_spp_rejected_client_reject(
        300
    );

    TEST_ASSERT_TRUE(
        wait_for_disconnect_count(
            3,
            1500
        )
    );

    settle_retry_worker();

    TEST_ASSERT_EQUAL_UINT32(
        3,
        observed_disconnect_count()
    );
}

static void test_close_event_cancels_pending_retry(void)
{
    reset_observation();

    set_disconnect_behavior(
        0,
        1
    );

    bt_spp_rejected_client_reject(
        400
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        observed_disconnect_count()
    );

    bt_spp_rejected_client_on_closed(
        400
    );

    settle_retry_worker();

    TEST_ASSERT_EQUAL_UINT32(
        1,
        observed_disconnect_count()
    );
}

static void test_active_handle_is_never_disconnected(void)
{
    reset_observation();

    set_active_handle(
        500
    );

    bt_spp_rejected_client_reject(
        500
    );

    settle_retry_worker();

    TEST_ASSERT_EQUAL_UINT32(
        0,
        observed_disconnect_count()
    );
}

static void test_pending_retry_stops_when_handle_becomes_active(void)
{
    reset_observation();

    set_disconnect_behavior(
        0,
        1
    );

    bt_spp_rejected_client_reject(
        600
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        observed_disconnect_count()
    );

    set_active_handle(
        600
    );

    settle_retry_worker();

    TEST_ASSERT_EQUAL_UINT32(
        1,
        observed_disconnect_count()
    );
}

static void test_zero_handle_is_ignored(void)
{
    reset_observation();

    bt_spp_rejected_client_reject(
        0
    );

    settle_retry_worker();

    TEST_ASSERT_EQUAL_UINT32(
        0,
        observed_disconnect_count()
    );
}

static void test_duplicate_rejection_replaces_stale_retry_token(void)
{
    reset_observation();

    set_disconnect_behavior(
        2,
        0
    );

    bt_spp_rejected_client_reject(
        700
    );

    bt_spp_rejected_client_reject(
        700
    );

    TEST_ASSERT_EQUAL_UINT32(
        2,
        observed_disconnect_count()
    );

    TEST_ASSERT_TRUE(
        wait_for_disconnect_count(
            3,
            1200
        )
    );

    settle_retry_worker();

    TEST_ASSERT_EQUAL_UINT32(
        3,
        observed_disconnect_count()
    );

    TEST_ASSERT_EQUAL_UINT32(
        700,
        observed_disconnect_handle(
            0
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        700,
        observed_disconnect_handle(
            1
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        700,
        observed_disconnect_handle(
            2
        )
    );

    bt_spp_rejected_client_on_closed(
        700
    );
}

void app_main(void)
{
    UNITY_BEGIN();

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        bt_spp_rejected_client_init(
            NULL
        )
    );

    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_spp_rejected_client_init(
            observe_guarded_disconnect
        )
    );

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        bt_spp_rejected_client_init(
            observe_guarded_disconnect
        )
    );

    RUN_TEST(
        test_immediate_disconnect_success_needs_no_retry
    );

    RUN_TEST(
        test_failed_disconnect_retries_and_succeeds
    );

    RUN_TEST(
        test_disconnect_retry_exhaustion_is_bounded
    );

    RUN_TEST(
        test_close_event_cancels_pending_retry
    );

    RUN_TEST(
        test_active_handle_is_never_disconnected
    );

    RUN_TEST(
        test_pending_retry_stops_when_handle_becomes_active
    );

    RUN_TEST(
        test_zero_handle_is_ignored
    );

    RUN_TEST(
        test_duplicate_rejection_replaces_stale_retry_token
    );

    UNITY_END();
}
