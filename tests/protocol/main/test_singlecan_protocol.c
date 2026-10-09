#include "protocol_test_doubles.h"
#include "singlecan_commands.h"

#include "unity.h"

static void test_valid_single_module_configuration(void)
{
    protocol_test_doubles_reset();
    singlecan_commands_reset_session();

    singlecan_commands_process(
        "{\"id\":1,\"type\":\"command\","
        "\"command\":\"config.module\","
        "\"value\":\"single\"}"
    );

    const protocol_test_response_capture_t *response =
        protocol_test_response_capture();

    TEST_ASSERT_EQUAL_INT(
        1,
        singlecan_commands_is_configured()
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        response->call_count
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        response->packet_id
    );

    TEST_ASSERT_EQUAL_STRING(
        "ok",
        response->status
    );

    TEST_ASSERT_EQUAL_STRING(
        "config.module",
        response->command
    );

    TEST_ASSERT_EQUAL_STRING(
        "",
        response->reason
    );

    TEST_ASSERT_EQUAL_INT(
        0,
        protocol_test_dispatch_call_count()
    );
}

void app_main(void)
{
    UNITY_BEGIN();

    RUN_TEST(
        test_valid_single_module_configuration
    );

    UNITY_END();
}
