#include "bt_spp_writer.h"

#include "unity.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


static volatile esp_err_t s_wrapped_write_result =
    ESP_ERR_INVALID_STATE;

static volatile uint32_t s_wrapped_write_count =
    0;

static volatile uint32_t s_wrapped_write_handle =
    0;

static volatile int s_wrapped_write_length =
    0;

static volatile int s_wrapped_write_has_newline =
    0;

static void reset_wrapped_write_observation(void)
{
    s_wrapped_write_result =
        ESP_ERR_INVALID_STATE;

    s_wrapped_write_count =
        0;

    s_wrapped_write_handle =
        0;

    s_wrapped_write_length =
        0;

    s_wrapped_write_has_newline =
        0;
}

esp_err_t __wrap_esp_spp_write(
    uint32_t handle,
    int length,
    uint8_t *data
)
{
    s_wrapped_write_handle =
        handle;

    s_wrapped_write_length =
        length;

    if (
        data != NULL &&
        length > 0 &&
        data[length - 1] == '\n'
    ) {
        s_wrapped_write_has_newline =
            1;
    }

    s_wrapped_write_count +=
        1;

    return s_wrapped_write_result;
}

static bt_spp_writer_session_t s_test_session = {
    .handle =
        100,
    .session_id =
        1,
    .server_ready =
        1,
    .connected =
        1,
    .congested =
        0,
};

static void set_valid_test_session(void)
{
    s_test_session.handle =
        100;

    s_test_session.session_id =
        1;

    s_test_session.server_ready =
        1;

    s_test_session.connected =
        1;

    s_test_session.congested =
        0;
}

static void valid_session_provider(
    bt_spp_writer_session_t *session
)
{
    if (session == NULL) {
        return;
    }

    *session =
        s_test_session;
}

static volatile uint32_t s_disconnect_count =
    0;

static volatile uint32_t s_disconnected_handle =
    0;

static volatile uint32_t s_disconnected_session_id =
    0;

static void reset_disconnect_observation(void)
{
    s_disconnect_count =
        0;

    s_disconnected_handle =
        0;

    s_disconnected_session_id =
        0;
}

static void valid_disconnect_handler(
    uint32_t handle,
    uint32_t session_id
)
{
    s_disconnected_handle =
        handle;

    s_disconnected_session_id =
        session_id;

    s_disconnect_count +=
        1;
}

static void test_init_rejects_missing_required_callbacks(void)
{
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        bt_spp_writer_init(
            NULL,
            valid_disconnect_handler
        )
    );

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        bt_spp_writer_init(
            valid_session_provider,
            NULL
        )
    );

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        bt_spp_writer_init(
            NULL,
            NULL
        )
    );
}


static void test_writer_initializes_once_with_zero_failure_stats(void)
{
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_spp_writer_init(
            valid_session_provider,
            valid_disconnect_handler
        )
    );

    bt_spp_writer_stats_t stats = {
        .queue_full =
            0xFFFFFFFF,
        .immediate_write_failures =
            0xFFFFFFFF,
        .asynchronous_write_failures =
            0xFFFFFFFF,
        .write_timeouts =
            0xFFFFFFFF,
    };

    bt_spp_writer_get_stats(
        &stats
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        stats.queue_full
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        stats.immediate_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        stats.asynchronous_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        stats.write_timeouts
    );

    bt_spp_writer_get_stats(
        NULL
    );

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        bt_spp_writer_init(
            valid_session_provider,
            valid_disconnect_handler
        )
    );
}


static void test_send_rejects_invalid_messages_without_failure_counts(void)
{
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        bt_spp_writer_send(
            NULL,
            1
        )
    );

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        bt_spp_writer_send(
            "",
            1
        )
    );

    char oversized_message[256];

    for (
        size_t index = 0;
        index < sizeof(oversized_message) - 1;
        ++index
    ) {
        oversized_message[index] =
            'A';
    }

    oversized_message[
        sizeof(oversized_message) - 1
    ] = '\0';

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_SIZE,
        bt_spp_writer_send(
            oversized_message,
            1
        )
    );

    bt_spp_writer_stats_t stats = {
        0
    };

    bt_spp_writer_get_stats(
        &stats
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        stats.queue_full
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        stats.immediate_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        stats.asynchronous_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        stats.write_timeouts
    );
}


static void test_send_rejects_unavailable_or_mismatched_session(void)
{
    set_valid_test_session();

    s_test_session.server_ready =
        0;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        bt_spp_writer_send(
            "not-ready",
            1
        )
    );

    set_valid_test_session();

    s_test_session.connected =
        0;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        bt_spp_writer_send(
            "not-connected",
            1
        )
    );

    set_valid_test_session();

    s_test_session.handle =
        0;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        bt_spp_writer_send(
            "zero-handle",
            1
        )
    );

    set_valid_test_session();

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        bt_spp_writer_send(
            "wrong-session",
            2
        )
    );

    bt_spp_writer_stats_t stats = {
        0
    };

    bt_spp_writer_get_stats(
        &stats
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        stats.queue_full
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        stats.immediate_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        stats.asynchronous_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        stats.write_timeouts
    );

    set_valid_test_session();
}


static void test_immediate_write_failure_is_counted_and_disconnects_session(void)
{
    set_valid_test_session();
    reset_disconnect_observation();

    bt_spp_writer_on_connected();

    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_spp_writer_send(
            "writer-immediate-failure-test",
            1
        )
    );

    const TickType_t deadline =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(
            2000
        );

    while (
        s_disconnect_count == 0 &&
        xTaskGetTickCount() < deadline
    ) {
        vTaskDelay(
            pdMS_TO_TICKS(
                10
            )
        );
    }

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_disconnect_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        s_disconnected_handle
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_disconnected_session_id
    );

    bt_spp_writer_stats_t stats = {
        0
    };

    bt_spp_writer_get_stats(
        &stats
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        stats.queue_full
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        stats.immediate_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        stats.asynchronous_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        stats.write_timeouts
    );

    bt_spp_writer_on_disconnected();
}


static void test_null_and_unrelated_write_events_are_ignored(void)
{
    bt_spp_writer_stats_t before = {
        0
    };

    bt_spp_writer_get_stats(
        &before
    );

    TEST_ASSERT_FALSE(
        bt_spp_writer_on_write_event(
            NULL
        )
    );

    esp_spp_cb_param_t unrelated_event = {
        0
    };

    unrelated_event.write.handle =
        999;

    unrelated_event.write.status =
        ESP_SPP_SUCCESS;

    unrelated_event.write.len =
        10;

    TEST_ASSERT_FALSE(
        bt_spp_writer_on_write_event(
            &unrelated_event
        )
    );

    bt_spp_writer_stats_t after = {
        0
    };

    bt_spp_writer_get_stats(
        &after
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.queue_full,
        after.queue_full
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.immediate_write_failures,
        after.immediate_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.asynchronous_write_failures,
        after.asynchronous_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.write_timeouts,
        after.write_timeouts
    );
}


static void test_queue_saturation_is_counted_and_disconnects_session(void)
{
    set_valid_test_session();

    s_test_session.congested =
        1;

    reset_disconnect_observation();

    bt_spp_writer_stats_t before = {
        0
    };

    bt_spp_writer_get_stats(
        &before
    );

    bt_spp_writer_on_connected();
    bt_spp_writer_on_congestion_changed();

    uint32_t accepted_count =
        0;

    esp_err_t send_result =
        ESP_OK;

    for (
        uint32_t attempt = 0;
        attempt < 64;
        ++attempt
    ) {
        send_result =
            bt_spp_writer_send(
                "queue-saturation-test",
                1
            );

        if (send_result == ESP_OK) {
            accepted_count +=
                1;

            continue;
        }

        break;
    }

    TEST_ASSERT_EQUAL(
        ESP_ERR_NO_MEM,
        send_result
    );

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(
        32,
        accepted_count
    );

    TEST_ASSERT_LESS_OR_EQUAL_UINT32(
        33,
        accepted_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_disconnect_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        s_disconnected_handle
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_disconnected_session_id
    );

    bt_spp_writer_stats_t after = {
        0
    };

    bt_spp_writer_get_stats(
        &after
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.queue_full + 1,
        after.queue_full
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.immediate_write_failures,
        after.immediate_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.asynchronous_write_failures,
        after.asynchronous_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.write_timeouts,
        after.write_timeouts
    );

    bt_spp_writer_on_disconnected();

    set_valid_test_session();
}


static void test_successful_write_event_completes_matching_inflight_write(void)
{
    set_valid_test_session();

    s_test_session.session_id =
        2;

    reset_disconnect_observation();
    reset_wrapped_write_observation();

    s_wrapped_write_result =
        ESP_OK;

    bt_spp_writer_stats_t before = {
        0
    };

    bt_spp_writer_get_stats(
        &before
    );

    bt_spp_writer_on_connected();
    bt_spp_writer_on_congestion_changed();

    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_spp_writer_send(
            "successful-write",
            2
        )
    );

    const TickType_t submission_deadline =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(
            2000
        );

    while (
        s_wrapped_write_count == 0 &&
        xTaskGetTickCount() < submission_deadline
    ) {
        vTaskDelay(
            pdMS_TO_TICKS(
                10
            )
        );
    }

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_wrapped_write_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        s_wrapped_write_handle
    );

    TEST_ASSERT_EQUAL(
        17,
        s_wrapped_write_length
    );

    TEST_ASSERT_TRUE(
        s_wrapped_write_has_newline
    );

    esp_spp_cb_param_t completion = {
        0
    };

    completion.write.handle =
        100;

    completion.write.status =
        ESP_SPP_SUCCESS;

    completion.write.len =
        s_wrapped_write_length;

    TEST_ASSERT_TRUE(
        bt_spp_writer_on_write_event(
            &completion
        )
    );

    vTaskDelay(
        pdMS_TO_TICKS(
            50
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_disconnect_count
    );

    bt_spp_writer_stats_t after = {
        0
    };

    bt_spp_writer_get_stats(
        &after
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.queue_full,
        after.queue_full
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.immediate_write_failures,
        after.immediate_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.asynchronous_write_failures,
        after.asynchronous_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.write_timeouts,
        after.write_timeouts
    );

    TEST_ASSERT_FALSE(
        bt_spp_writer_on_write_event(
            &completion
        )
    );

    bt_spp_writer_on_disconnected();

    s_wrapped_write_result =
        ESP_ERR_INVALID_STATE;

    set_valid_test_session();
}


static void test_same_handle_wrong_length_completion_is_ignored(void)
{
    set_valid_test_session();

    s_test_session.session_id =
        6;

    reset_disconnect_observation();
    reset_wrapped_write_observation();

    s_wrapped_write_result =
        ESP_OK;

    bt_spp_writer_stats_t before = {
        0
    };

    bt_spp_writer_get_stats(
        &before
    );

    bt_spp_writer_on_connected();
    bt_spp_writer_on_congestion_changed();

    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_spp_writer_send(
            "new-session-write",
            6
        )
    );

    const TickType_t submission_deadline =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(
            2000
        );

    while (
        s_wrapped_write_count == 0 &&
        xTaskGetTickCount() < submission_deadline
    ) {
        vTaskDelay(
            pdMS_TO_TICKS(
                10
            )
        );
    }

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_wrapped_write_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        s_wrapped_write_handle
    );

    TEST_ASSERT_EQUAL(
        18,
        s_wrapped_write_length
    );

    esp_spp_cb_param_t stale_completion = {
        0
    };

    stale_completion.write.handle =
        100;

    stale_completion.write.status =
        ESP_SPP_SUCCESS;

    stale_completion.write.len =
        s_wrapped_write_length - 1;

    TEST_ASSERT_FALSE(
        bt_spp_writer_on_write_event(
            &stale_completion
        )
    );

    vTaskDelay(
        pdMS_TO_TICKS(
            100
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_disconnect_count
    );

    esp_spp_cb_param_t real_completion = {
        0
    };

    real_completion.write.handle =
        100;

    real_completion.write.status =
        ESP_SPP_SUCCESS;

    real_completion.write.len =
        s_wrapped_write_length;

    TEST_ASSERT_TRUE(
        bt_spp_writer_on_write_event(
            &real_completion
        )
    );

    vTaskDelay(
        pdMS_TO_TICKS(
            100
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_disconnect_count
    );

    bt_spp_writer_stats_t after = {
        0
    };

    bt_spp_writer_get_stats(
        &after
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.queue_full,
        after.queue_full
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.immediate_write_failures,
        after.immediate_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.asynchronous_write_failures,
        after.asynchronous_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.write_timeouts,
        after.write_timeouts
    );

    TEST_ASSERT_FALSE(
        bt_spp_writer_on_write_event(
            &real_completion
        )
    );

    bt_spp_writer_on_disconnected();

    s_wrapped_write_result =
        ESP_ERR_INVALID_STATE;

    set_valid_test_session();
}



static void test_failed_write_event_is_counted_and_disconnects_session(void)
{
    set_valid_test_session();

    s_test_session.session_id =
        3;

    reset_disconnect_observation();
    reset_wrapped_write_observation();

    s_wrapped_write_result =
        ESP_OK;

    bt_spp_writer_stats_t before = {
        0
    };

    bt_spp_writer_get_stats(
        &before
    );

    bt_spp_writer_on_connected();
    bt_spp_writer_on_congestion_changed();

    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_spp_writer_send(
            "asynchronous-failure",
            3
        )
    );

    const TickType_t submission_deadline =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(
            2000
        );

    while (
        s_wrapped_write_count == 0 &&
        xTaskGetTickCount() < submission_deadline
    ) {
        vTaskDelay(
            pdMS_TO_TICKS(
                10
            )
        );
    }

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_wrapped_write_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        s_wrapped_write_handle
    );

    esp_spp_cb_param_t failure_event = {
        0
    };

    failure_event.write.handle =
        100;

    failure_event.write.status =
        ESP_SPP_FAILURE;

    failure_event.write.len =
        s_wrapped_write_length;

    TEST_ASSERT_TRUE(
        bt_spp_writer_on_write_event(
            &failure_event
        )
    );

    const TickType_t failure_deadline =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(
            2000
        );

    while (
        s_disconnect_count == 0 &&
        xTaskGetTickCount() < failure_deadline
    ) {
        vTaskDelay(
            pdMS_TO_TICKS(
                10
            )
        );
    }

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_disconnect_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        s_disconnected_handle
    );

    TEST_ASSERT_EQUAL_UINT32(
        3,
        s_disconnected_session_id
    );

    bt_spp_writer_stats_t after = {
        0
    };

    bt_spp_writer_get_stats(
        &after
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.queue_full,
        after.queue_full
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.immediate_write_failures,
        after.immediate_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.asynchronous_write_failures + 1,
        after.asynchronous_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.write_timeouts,
        after.write_timeouts
    );

    TEST_ASSERT_FALSE(
        bt_spp_writer_on_write_event(
            &failure_event
        )
    );

    bt_spp_writer_on_disconnected();

    s_wrapped_write_result =
        ESP_ERR_INVALID_STATE;

    set_valid_test_session();
}


static void test_disconnect_wakes_and_cancels_inflight_write(void)
{
    set_valid_test_session();

    s_test_session.session_id =
        4;

    reset_disconnect_observation();
    reset_wrapped_write_observation();

    s_wrapped_write_result =
        ESP_OK;

    bt_spp_writer_stats_t before = {
        0
    };

    bt_spp_writer_get_stats(
        &before
    );

    bt_spp_writer_on_connected();
    bt_spp_writer_on_congestion_changed();

    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_spp_writer_send(
            "disconnect-inflight",
            4
        )
    );

    const TickType_t submission_deadline =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(
            2000
        );

    while (
        s_wrapped_write_count == 0 &&
        xTaskGetTickCount() < submission_deadline
    ) {
        vTaskDelay(
            pdMS_TO_TICKS(
                10
            )
        );
    }

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_wrapped_write_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        s_wrapped_write_handle
    );

    const TickType_t disconnect_start =
        xTaskGetTickCount();

    bt_spp_writer_on_disconnected();

    const TickType_t failure_deadline =
        disconnect_start +
        pdMS_TO_TICKS(
            2000
        );

    while (
        s_disconnect_count == 0 &&
        xTaskGetTickCount() < failure_deadline
    ) {
        vTaskDelay(
            pdMS_TO_TICKS(
                10
            )
        );
    }

    const TickType_t disconnect_elapsed =
        xTaskGetTickCount() -
        disconnect_start;

    TEST_ASSERT_LESS_THAN_UINT32(
        pdMS_TO_TICKS(
            2000
        ),
        disconnect_elapsed
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_disconnect_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        s_disconnected_handle
    );

    TEST_ASSERT_EQUAL_UINT32(
        4,
        s_disconnected_session_id
    );

    bt_spp_writer_stats_t after = {
        0
    };

    bt_spp_writer_get_stats(
        &after
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.queue_full,
        after.queue_full
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.immediate_write_failures,
        after.immediate_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.asynchronous_write_failures + 1,
        after.asynchronous_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.write_timeouts,
        after.write_timeouts
    );

    esp_spp_cb_param_t late_completion = {
        0
    };

    late_completion.write.handle =
        100;

    late_completion.write.status =
        ESP_SPP_SUCCESS;

    late_completion.write.len =
        s_wrapped_write_length;

    TEST_ASSERT_FALSE(
        bt_spp_writer_on_write_event(
            &late_completion
        )
    );

    s_wrapped_write_result =
        ESP_ERR_INVALID_STATE;

    set_valid_test_session();
}


static void test_ordered_close_rejects_old_completion_before_same_handle_reuse(void)
{
    set_valid_test_session();

    s_test_session.session_id =
        7;

    reset_disconnect_observation();
    reset_wrapped_write_observation();

    s_wrapped_write_result =
        ESP_OK;

    bt_spp_writer_on_connected();
    bt_spp_writer_on_congestion_changed();

    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_spp_writer_send(
            "same-length-write",
            7
        )
    );

    const TickType_t first_submission_deadline =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(
            2000
        );

    while (
        s_wrapped_write_count < 1 &&
        xTaskGetTickCount() <
            first_submission_deadline
    ) {
        vTaskDelay(
            pdMS_TO_TICKS(
                10
            )
        );
    }

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_wrapped_write_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        s_wrapped_write_handle
    );

    const int shared_write_length =
        s_wrapped_write_length;

    TEST_ASSERT_GREATER_THAN(
        0,
        shared_write_length
    );

    bt_spp_writer_stats_t before_disconnect = {
        0
    };

    bt_spp_writer_get_stats(
        &before_disconnect
    );

    s_test_session.connected =
        0;

    bt_spp_writer_on_disconnected();

    const TickType_t cancellation_deadline =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(
            2000
        );

    while (
        s_disconnect_count == 0 &&
        xTaskGetTickCount() <
            cancellation_deadline
    ) {
        vTaskDelay(
            pdMS_TO_TICKS(
                10
            )
        );
    }

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_disconnect_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        s_disconnected_handle
    );

    TEST_ASSERT_EQUAL_UINT32(
        7,
        s_disconnected_session_id
    );

    bt_spp_writer_stats_t after_disconnect = {
        0
    };

    bt_spp_writer_get_stats(
        &after_disconnect
    );

    TEST_ASSERT_EQUAL_UINT32(
        before_disconnect.asynchronous_write_failures + 1,
        after_disconnect.asynchronous_write_failures
    );

    esp_spp_cb_param_t old_completion = {
        0
    };

    old_completion.write.handle =
        100;

    old_completion.write.status =
        ESP_SPP_SUCCESS;

    old_completion.write.len =
        shared_write_length;

    TEST_ASSERT_FALSE(
        bt_spp_writer_on_write_event(
            &old_completion
        )
    );

    s_test_session.handle =
        100;

    s_test_session.session_id =
        8;

    s_test_session.connected =
        1;

    s_test_session.server_ready =
        1;

    s_test_session.congested =
        0;

    bt_spp_writer_on_connected();
    bt_spp_writer_on_congestion_changed();

    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_spp_writer_send(
            "same-length-write",
            8
        )
    );

    const TickType_t second_submission_deadline =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(
            2000
        );

    while (
        s_wrapped_write_count < 2 &&
        xTaskGetTickCount() <
            second_submission_deadline
    ) {
        vTaskDelay(
            pdMS_TO_TICKS(
                10
            )
        );
    }

    TEST_ASSERT_EQUAL_UINT32(
        2,
        s_wrapped_write_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        s_wrapped_write_handle
    );

    TEST_ASSERT_EQUAL(
        shared_write_length,
        s_wrapped_write_length
    );

    esp_spp_cb_param_t new_completion = {
        0
    };

    new_completion.write.handle =
        100;

    new_completion.write.status =
        ESP_SPP_SUCCESS;

    new_completion.write.len =
        shared_write_length;

    TEST_ASSERT_TRUE(
        bt_spp_writer_on_write_event(
            &new_completion
        )
    );

    vTaskDelay(
        pdMS_TO_TICKS(
            100
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_disconnect_count
    );

    TEST_ASSERT_FALSE(
        bt_spp_writer_on_write_event(
            &new_completion
        )
    );

    bt_spp_writer_on_disconnected();

    s_wrapped_write_result =
        ESP_ERR_INVALID_STATE;

    set_valid_test_session();
}


static void test_write_completion_timeout_is_counted_and_disconnects_session(void)
{
    set_valid_test_session();

    s_test_session.session_id =
        5;

    reset_disconnect_observation();
    reset_wrapped_write_observation();

    s_wrapped_write_result =
        ESP_OK;

    bt_spp_writer_stats_t before = {
        0
    };

    bt_spp_writer_get_stats(
        &before
    );

    bt_spp_writer_on_connected();
    bt_spp_writer_on_congestion_changed();

    const TickType_t timeout_start =
        xTaskGetTickCount();

    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_spp_writer_send(
            "completion-timeout",
            5
        )
    );

    const TickType_t submission_deadline =
        timeout_start +
        pdMS_TO_TICKS(
            2000
        );

    while (
        s_wrapped_write_count == 0 &&
        xTaskGetTickCount() < submission_deadline
    ) {
        vTaskDelay(
            pdMS_TO_TICKS(
                10
            )
        );
    }

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_wrapped_write_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        s_wrapped_write_handle
    );

    const TickType_t timeout_deadline =
        timeout_start +
        pdMS_TO_TICKS(
            7000
        );

    while (
        s_disconnect_count == 0 &&
        xTaskGetTickCount() < timeout_deadline
    ) {
        vTaskDelay(
            pdMS_TO_TICKS(
                10
            )
        );
    }

    const TickType_t timeout_elapsed =
        xTaskGetTickCount() -
        timeout_start;

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(
        pdMS_TO_TICKS(
            4900
        ),
        timeout_elapsed
    );

    TEST_ASSERT_LESS_THAN_UINT32(
        pdMS_TO_TICKS(
            7000
        ),
        timeout_elapsed
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_disconnect_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        s_disconnected_handle
    );

    TEST_ASSERT_EQUAL_UINT32(
        5,
        s_disconnected_session_id
    );

    bt_spp_writer_stats_t after = {
        0
    };

    bt_spp_writer_get_stats(
        &after
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.queue_full,
        after.queue_full
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.immediate_write_failures,
        after.immediate_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.asynchronous_write_failures,
        after.asynchronous_write_failures
    );

    TEST_ASSERT_EQUAL_UINT32(
        before.write_timeouts + 1,
        after.write_timeouts
    );

    esp_spp_cb_param_t late_completion = {
        0
    };

    late_completion.write.handle =
        100;

    late_completion.write.status =
        ESP_SPP_SUCCESS;

    late_completion.write.len =
        s_wrapped_write_length;

    TEST_ASSERT_FALSE(
        bt_spp_writer_on_write_event(
            &late_completion
        )
    );

    bt_spp_writer_on_disconnected();

    s_wrapped_write_result =
        ESP_ERR_INVALID_STATE;

    set_valid_test_session();
}

void app_main(void)
{
    UNITY_BEGIN();

    RUN_TEST(
        test_init_rejects_missing_required_callbacks
    );


    RUN_TEST(
        test_writer_initializes_once_with_zero_failure_stats
    );


    RUN_TEST(
        test_send_rejects_invalid_messages_without_failure_counts
    );


    RUN_TEST(
        test_send_rejects_unavailable_or_mismatched_session
    );


    RUN_TEST(
        test_immediate_write_failure_is_counted_and_disconnects_session
    );


    RUN_TEST(
        test_null_and_unrelated_write_events_are_ignored
    );


    RUN_TEST(
        test_queue_saturation_is_counted_and_disconnects_session
    );


    RUN_TEST(
        test_successful_write_event_completes_matching_inflight_write
    );


    RUN_TEST(
        test_same_handle_wrong_length_completion_is_ignored
    );


    RUN_TEST(
        test_failed_write_event_is_counted_and_disconnects_session
    );


    RUN_TEST(
        test_disconnect_wakes_and_cancels_inflight_write
    );


    RUN_TEST(
        test_ordered_close_rejects_old_completion_before_same_handle_reuse
    );


    RUN_TEST(
        test_write_completion_timeout_is_counted_and_disconnects_session
    );

    UNITY_END();
}
