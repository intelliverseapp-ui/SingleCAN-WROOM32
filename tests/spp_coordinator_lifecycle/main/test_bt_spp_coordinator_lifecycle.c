#include "bt_spp_coordinator_test.h"
#include "bt_spp_framer.h"
#include "bt_spp_session.h"
#include "bt_spp_writer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "unity.h"

#include <stddef.h>
#include <stdint.h>

#define TEST_EVENT_RESET_ENTERED BIT0
#define TEST_EVENT_RELEASE_RESET BIT1
#define TEST_EVENT_FORCE_CLOSE_DONE BIT2
#define TEST_EVENT_ACCEPT_STARTED BIT3
#define TEST_EVENT_ACCEPT_DONE BIT4
#define TEST_EVENT_DISCONNECT_ENTERED BIT5
#define TEST_EVENT_RELEASE_DISCONNECT BIT6
#define TEST_EVENT_GUARDED_DISCONNECT_DONE BIT7
#define TEST_EVENT_REPLACEMENT_ACCEPT_DONE BIT8

#define TEST_TASK_STACK_SIZE 4096
#define TEST_TASK_PRIORITY 5

#define TEST_WAIT_TIMEOUT_MS 2000
#define TEST_BLOCKING_OBSERVATION_MS 150

typedef struct {
    uint32_t handle;
    uint32_t session_id;
    int result;
} force_close_task_context_t;

typedef struct {
    uint32_t handle;
    uint32_t session_id;
    int result;
} accept_task_context_t;

typedef struct {
    uint32_t handle;
    esp_err_t disconnect_result;
    int issued;
} guarded_disconnect_task_context_t;

static EventGroupHandle_t s_test_events =
    NULL;

static portMUX_TYPE s_observation_lock =
    portMUX_INITIALIZER_UNLOCKED;

static int s_block_next_command_reset =
    0;

static uint32_t s_command_reset_count =
    0;

static uint32_t s_framer_reset_count =
    0;

static uint32_t s_writer_connected_count =
    0;

static uint32_t s_writer_disconnected_count =
    0;

static uint32_t s_transition_sequence =
    0;

static uint32_t s_writer_connected_sequence =
    0;

static uint32_t s_writer_disconnected_sequence =
    0;

static uint32_t s_disconnect_call_count =
    0;

static uint32_t s_disconnect_handle =
    0;

static void reset_observation(void)
{
    portENTER_CRITICAL(
        &s_observation_lock
    );

    s_block_next_command_reset =
        0;

    s_command_reset_count =
        0;

    s_framer_reset_count =
        0;

    s_writer_connected_count =
        0;

    s_writer_disconnected_count =
        0;

    s_transition_sequence =
        0;

    s_writer_connected_sequence =
        0;

    s_writer_disconnected_sequence =
        0;

    s_disconnect_call_count =
        0;

    s_disconnect_handle =
        0;

    portEXIT_CRITICAL(
        &s_observation_lock
    );

    xEventGroupClearBits(
        s_test_events,
        TEST_EVENT_RESET_ENTERED |
            TEST_EVENT_RELEASE_RESET |
            TEST_EVENT_FORCE_CLOSE_DONE |
            TEST_EVENT_ACCEPT_STARTED |
            TEST_EVENT_ACCEPT_DONE |
            TEST_EVENT_DISCONNECT_ENTERED |
            TEST_EVENT_RELEASE_DISCONNECT |
            TEST_EVENT_GUARDED_DISCONNECT_DONE |
            TEST_EVENT_REPLACEMENT_ACCEPT_DONE
    );
}

static uint32_t read_counter(
    uint32_t *counter
)
{
    uint32_t value;

    portENTER_CRITICAL(
        &s_observation_lock
    );

    value =
        *counter;

    portEXIT_CRITICAL(
        &s_observation_lock
    );

    return value;
}

static void arm_blocking_command_reset(void)
{
    portENTER_CRITICAL(
        &s_observation_lock
    );

    s_block_next_command_reset =
        1;

    portEXIT_CRITICAL(
        &s_observation_lock
    );
}

esp_err_t __wrap_esp_spp_disconnect(
    uint32_t handle
)
{
    portENTER_CRITICAL(
        &s_observation_lock
    );

    s_disconnect_call_count +=
        1;

    s_disconnect_handle =
        handle;

    portEXIT_CRITICAL(
        &s_observation_lock
    );

    xEventGroupSetBits(
        s_test_events,
        TEST_EVENT_DISCONNECT_ENTERED
    );

    (void)xEventGroupWaitBits(
        s_test_events,
        TEST_EVENT_RELEASE_DISCONNECT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY
    );

    return ESP_OK;
}

void singlecan_commands_reset_session(void)
{
    int should_block =
        0;

    portENTER_CRITICAL(
        &s_observation_lock
    );

    s_command_reset_count +=
        1;

    if (s_block_next_command_reset) {
        s_block_next_command_reset =
            0;

        should_block =
            1;
    }

    portEXIT_CRITICAL(
        &s_observation_lock
    );

    if (!should_block) {
        return;
    }

    xEventGroupSetBits(
        s_test_events,
        TEST_EVENT_RESET_ENTERED
    );

    (void)xEventGroupWaitBits(
        s_test_events,
        TEST_EVENT_RELEASE_RESET,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY
    );
}

void singlecan_commands_process(
    const char *frame
)
{
    (void)frame;
}

void bt_spp_framer_reset(void)
{
    portENTER_CRITICAL(
        &s_observation_lock
    );

    s_framer_reset_count +=
        1;

    portEXIT_CRITICAL(
        &s_observation_lock
    );
}

int bt_spp_framer_init(
    bt_spp_frame_handler_t handler
)
{
    return handler != NULL;
}

void bt_spp_framer_process(
    const uint8_t *data,
    size_t data_length
)
{
    (void)data;
    (void)data_length;
}

void bt_spp_writer_on_connected(void)
{
    portENTER_CRITICAL(
        &s_observation_lock
    );

    s_writer_connected_count +=
        1;

    s_transition_sequence +=
        1;

    s_writer_connected_sequence =
        s_transition_sequence;

    portEXIT_CRITICAL(
        &s_observation_lock
    );
}

void bt_spp_writer_on_disconnected(void)
{
    portENTER_CRITICAL(
        &s_observation_lock
    );

    s_writer_disconnected_count +=
        1;

    s_transition_sequence +=
        1;

    s_writer_disconnected_sequence =
        s_transition_sequence;

    portEXIT_CRITICAL(
        &s_observation_lock
    );
}

static void force_close_task(
    void *task_argument
)
{
    force_close_task_context_t *context =
        task_argument;

    context->result =
        bt_spp_force_close_session_transition(
            context->handle,
            context->session_id
        );

    xEventGroupSetBits(
        s_test_events,
        TEST_EVENT_FORCE_CLOSE_DONE
    );

    vTaskDelete(
        NULL
    );
}

static void accept_task(
    void *task_argument
)
{
    accept_task_context_t *context =
        task_argument;

    xEventGroupSetBits(
        s_test_events,
        TEST_EVENT_ACCEPT_STARTED
    );

    context->result =
        bt_spp_accept_session_transition(
            context->handle,
            &context->session_id
        );

    xEventGroupSetBits(
        s_test_events,
        TEST_EVENT_ACCEPT_DONE
    );

    vTaskDelete(
        NULL
    );
}

static void guarded_disconnect_task(
    void *task_argument
)
{
    guarded_disconnect_task_context_t *context =
        task_argument;

    context->issued =
        bt_spp_disconnect_rejected_client(
            context->handle,
            &context->disconnect_result
        );

    xEventGroupSetBits(
        s_test_events,
        TEST_EVENT_GUARDED_DISCONNECT_DONE
    );

    vTaskDelete(
        NULL
    );
}

static void replacement_accept_task(
    void *task_argument
)
{
    accept_task_context_t *context =
        task_argument;

    context->result =
        bt_spp_accept_session_transition(
            context->handle,
            &context->session_id
        );

    xEventGroupSetBits(
        s_test_events,
        TEST_EVENT_REPLACEMENT_ACCEPT_DONE
    );

    vTaskDelete(
        NULL
    );
}

static void run_replacement_serialization_case(
    uint32_t original_handle,
    uint32_t replacement_handle
)
{
    reset_observation();

    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_spp_session_lifecycle_init()
    );

    uint32_t original_session_id =
        0;

    TEST_ASSERT_TRUE(
        bt_spp_accept_session_transition(
            original_handle,
            &original_session_id
        )
    );

    TEST_ASSERT_NOT_EQUAL(
        0,
        original_session_id
    );

    TEST_ASSERT_EQUAL_UINT32(
        original_handle,
        bt_spp_session_get_handle()
    );

    arm_blocking_command_reset();

    force_close_task_context_t force_context = {
        .handle =
            original_handle,
        .session_id =
            original_session_id,
        .result =
            0
    };

    accept_task_context_t accept_context = {
        .handle =
            replacement_handle,
        .session_id =
            0,
        .result =
            0
    };

    TEST_ASSERT_EQUAL(
        pdPASS,
        xTaskCreate(
            force_close_task,
            "force_close",
            TEST_TASK_STACK_SIZE,
            &force_context,
            TEST_TASK_PRIORITY,
            NULL
        )
    );

    const EventBits_t reset_entered =
        xEventGroupWaitBits(
            s_test_events,
            TEST_EVENT_RESET_ENTERED,
            pdFALSE,
            pdTRUE,
            pdMS_TO_TICKS(
                TEST_WAIT_TIMEOUT_MS
            )
        );

    TEST_ASSERT_BITS_HIGH(
        TEST_EVENT_RESET_ENTERED,
        reset_entered
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL(
        pdPASS,
        xTaskCreate(
            accept_task,
            "replacement",
            TEST_TASK_STACK_SIZE,
            &accept_context,
            TEST_TASK_PRIORITY,
            NULL
        )
    );

    const EventBits_t accept_started =
        xEventGroupWaitBits(
            s_test_events,
            TEST_EVENT_ACCEPT_STARTED,
            pdFALSE,
            pdTRUE,
            pdMS_TO_TICKS(
                TEST_WAIT_TIMEOUT_MS
            )
        );

    TEST_ASSERT_BITS_HIGH(
        TEST_EVENT_ACCEPT_STARTED,
        accept_started
    );

    vTaskDelay(
        pdMS_TO_TICKS(
            TEST_BLOCKING_OBSERVATION_MS
        )
    );

    const EventBits_t blocked_bits =
        xEventGroupGetBits(
            s_test_events
        );

    TEST_ASSERT_BITS_LOW(
        TEST_EVENT_ACCEPT_DONE,
        blocked_bits
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_is_connected()
    );

    xEventGroupSetBits(
        s_test_events,
        TEST_EVENT_RELEASE_RESET
    );

    const EventBits_t completed_bits =
        xEventGroupWaitBits(
            s_test_events,
            TEST_EVENT_FORCE_CLOSE_DONE |
                TEST_EVENT_ACCEPT_DONE,
            pdFALSE,
            pdTRUE,
            pdMS_TO_TICKS(
                TEST_WAIT_TIMEOUT_MS
            )
        );

    TEST_ASSERT_BITS_HIGH(
        TEST_EVENT_FORCE_CLOSE_DONE |
            TEST_EVENT_ACCEPT_DONE,
        completed_bits
    );

    TEST_ASSERT_TRUE(
        force_context.result
    );

    TEST_ASSERT_TRUE(
        accept_context.result
    );

    TEST_ASSERT_NOT_EQUAL(
        0,
        accept_context.session_id
    );

    TEST_ASSERT_NOT_EQUAL(
        original_session_id,
        accept_context.session_id
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_matches(
            replacement_handle,
            accept_context.session_id
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        replacement_handle,
        bt_spp_session_get_handle()
    );

    TEST_ASSERT_EQUAL_UINT32(
        3,
        read_counter(
            &s_command_reset_count
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        3,
        read_counter(
            &s_framer_reset_count
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        2,
        read_counter(
            &s_writer_connected_count
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        read_counter(
            &s_writer_disconnected_count
        )
    );

    TEST_ASSERT_LESS_THAN_UINT32(
        read_counter(
            &s_writer_connected_sequence
        ),
        read_counter(
            &s_writer_disconnected_sequence
        )
    );
}

static void test_same_handle_replacement_waits_for_old_cleanup(void)
{
    run_replacement_serialization_case(
        100,
        100
    );
}

static void test_different_handle_replacement_waits_for_old_cleanup(void)
{
    run_replacement_serialization_case(
        100,
        200
    );
}

static void test_same_handle_replacement_waits_for_guarded_disconnect(void)
{
    reset_observation();

    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_spp_session_lifecycle_init()
    );

    guarded_disconnect_task_context_t disconnect_context = {
        .handle =
            300,
        .disconnect_result =
            ESP_ERR_INVALID_STATE,
        .issued =
            0
    };

    accept_task_context_t replacement_context = {
        .handle =
            300,
        .session_id =
            0,
        .result =
            0
    };

    TEST_ASSERT_EQUAL(
        pdPASS,
        xTaskCreate(
            guarded_disconnect_task,
            "guarded_disconnect",
            TEST_TASK_STACK_SIZE,
            &disconnect_context,
            TEST_TASK_PRIORITY,
            NULL
        )
    );

    const EventBits_t disconnect_entered =
        xEventGroupWaitBits(
            s_test_events,
            TEST_EVENT_DISCONNECT_ENTERED,
            pdFALSE,
            pdTRUE,
            pdMS_TO_TICKS(
                TEST_WAIT_TIMEOUT_MS
            )
        );

    TEST_ASSERT_BITS_HIGH(
        TEST_EVENT_DISCONNECT_ENTERED,
        disconnect_entered
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL(
        pdPASS,
        xTaskCreate(
            replacement_accept_task,
            "guarded_replacement",
            TEST_TASK_STACK_SIZE,
            &replacement_context,
            TEST_TASK_PRIORITY,
            NULL
        )
    );

    vTaskDelay(
        pdMS_TO_TICKS(
            TEST_BLOCKING_OBSERVATION_MS
        )
    );

    const EventBits_t blocked_bits =
        xEventGroupGetBits(
            s_test_events
        );

    TEST_ASSERT_BITS_LOW(
        TEST_EVENT_REPLACEMENT_ACCEPT_DONE,
        blocked_bits
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_is_connected()
    );

    xEventGroupSetBits(
        s_test_events,
        TEST_EVENT_RELEASE_DISCONNECT
    );

    const EventBits_t completed_bits =
        xEventGroupWaitBits(
            s_test_events,
            TEST_EVENT_GUARDED_DISCONNECT_DONE |
                TEST_EVENT_REPLACEMENT_ACCEPT_DONE,
            pdFALSE,
            pdTRUE,
            pdMS_TO_TICKS(
                TEST_WAIT_TIMEOUT_MS
            )
        );

    TEST_ASSERT_BITS_HIGH(
        TEST_EVENT_GUARDED_DISCONNECT_DONE |
            TEST_EVENT_REPLACEMENT_ACCEPT_DONE,
        completed_bits
    );

    TEST_ASSERT_TRUE(
        disconnect_context.issued
    );

    TEST_ASSERT_EQUAL(
        ESP_OK,
        disconnect_context.disconnect_result
    );

    TEST_ASSERT_TRUE(
        replacement_context.result
    );

    TEST_ASSERT_NOT_EQUAL(
        0,
        replacement_context.session_id
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_matches(
            300,
            replacement_context.session_id
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        read_counter(
            &s_disconnect_call_count
        )
    );

    TEST_ASSERT_EQUAL_UINT32(
        300,
        read_counter(
            &s_disconnect_handle
        )
    );
}

void app_main(void)
{
    s_test_events =
        xEventGroupCreate();

    TEST_ASSERT_NOT_NULL(
        s_test_events
    );

    UNITY_BEGIN();

    RUN_TEST(
        test_same_handle_replacement_waits_for_old_cleanup
    );

    RUN_TEST(
        test_different_handle_replacement_waits_for_old_cleanup
    );

    RUN_TEST(
        test_same_handle_replacement_waits_for_guarded_disconnect
    );

    UNITY_END();
}
