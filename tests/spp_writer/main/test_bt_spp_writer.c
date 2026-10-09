#include "bt_spp_writer.h"

#include "unity.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

    UNITY_END();
}
