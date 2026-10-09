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


static void assert_packet_is_silently_rejected(
    const char *packet
)
{
    protocol_test_doubles_reset();
    singlecan_commands_reset_session();

    singlecan_commands_process(
        packet
    );

    const protocol_test_response_capture_t *response =
        protocol_test_response_capture();

    TEST_ASSERT_EQUAL_INT(
        0,
        response->call_count
    );

    TEST_ASSERT_EQUAL_INT(
        0,
        protocol_test_dispatch_call_count()
    );

    TEST_ASSERT_EQUAL_INT(
        0,
        singlecan_commands_is_configured()
    );
}

static void test_malformed_json_is_rejected(void)
{
    assert_packet_is_silently_rejected(
        "{\"id\":1,\"type\":\"command\""
    );
}

static void test_trailing_data_is_rejected(void)
{
    assert_packet_is_silently_rejected(
        "{\"id\":1,\"type\":\"command\","
        "\"command\":\"LOCK_DOORS\"} trailing"
    );
}

static void test_non_object_root_is_rejected(void)
{
    assert_packet_is_silently_rejected(
        "[1,2,3]"
    );
}

static void test_missing_id_is_rejected(void)
{
    assert_packet_is_silently_rejected(
        "{\"type\":\"command\","
        "\"command\":\"LOCK_DOORS\"}"
    );
}

static void test_missing_type_is_rejected(void)
{
    assert_packet_is_silently_rejected(
        "{\"id\":1,"
        "\"command\":\"LOCK_DOORS\"}"
    );
}

static void test_missing_command_is_rejected(void)
{
    assert_packet_is_silently_rejected(
        "{\"id\":1,"
        "\"type\":\"command\"}"
    );
}

static void test_duplicate_id_is_rejected(void)
{
    assert_packet_is_silently_rejected(
        "{\"id\":1,\"id\":2,"
        "\"type\":\"command\","
        "\"command\":\"LOCK_DOORS\"}"
    );
}

static void test_duplicate_command_is_rejected(void)
{
    assert_packet_is_silently_rejected(
        "{\"id\":1,"
        "\"type\":\"command\","
        "\"command\":\"LOCK_DOORS\","
        "\"command\":\"UNLOCK_DOORS\"}"
    );
}

static void test_unknown_field_is_rejected(void)
{
    assert_packet_is_silently_rejected(
        "{\"id\":1,"
        "\"type\":\"command\","
        "\"command\":\"LOCK_DOORS\","
        "\"payload\":\"unsafe\"}"
    );
}

static void test_string_id_is_rejected(void)
{
    assert_packet_is_silently_rejected(
        "{\"id\":\"1\","
        "\"type\":\"command\","
        "\"command\":\"LOCK_DOORS\"}"
    );
}

static void test_fractional_id_is_rejected(void)
{
    assert_packet_is_silently_rejected(
        "{\"id\":1.5,"
        "\"type\":\"command\","
        "\"command\":\"LOCK_DOORS\"}"
    );
}

static void test_negative_id_is_rejected(void)
{
    assert_packet_is_silently_rejected(
        "{\"id\":-1,"
        "\"type\":\"command\","
        "\"command\":\"LOCK_DOORS\"}"
    );
}

static void test_wrong_type_value_is_rejected(void)
{
    assert_packet_is_silently_rejected(
        "{\"id\":1,"
        "\"type\":\"event\","
        "\"command\":\"LOCK_DOORS\"}"
    );
}

static void test_non_string_command_is_rejected(void)
{
    assert_packet_is_silently_rejected(
        "{\"id\":1,"
        "\"type\":\"command\","
        "\"command\":42}"
    );
}

static void test_empty_command_is_rejected(void)
{
    assert_packet_is_silently_rejected(
        "{\"id\":1,"
        "\"type\":\"command\","
        "\"command\":\"\"}"
    );
}

static void test_escaped_nul_is_rejected(void)
{
    assert_packet_is_silently_rejected(
        "{\"id\":1,"
        "\"type\":\"command\","
        "\"command\":\"LOCK\\u0000DOORS\"}"
    );
}

void app_main(void)
{
    UNITY_BEGIN();

    RUN_TEST(
        test_valid_single_module_configuration
    );


    RUN_TEST(
        test_malformed_json_is_rejected
    );

    RUN_TEST(
        test_trailing_data_is_rejected
    );

    RUN_TEST(
        test_non_object_root_is_rejected
    );

    RUN_TEST(
        test_missing_id_is_rejected
    );

    RUN_TEST(
        test_missing_type_is_rejected
    );

    RUN_TEST(
        test_missing_command_is_rejected
    );

    RUN_TEST(
        test_duplicate_id_is_rejected
    );

    RUN_TEST(
        test_duplicate_command_is_rejected
    );

    RUN_TEST(
        test_unknown_field_is_rejected
    );

    RUN_TEST(
        test_string_id_is_rejected
    );

    RUN_TEST(
        test_fractional_id_is_rejected
    );

    RUN_TEST(
        test_negative_id_is_rejected
    );

    RUN_TEST(
        test_wrong_type_value_is_rejected
    );

    RUN_TEST(
        test_non_string_command_is_rejected
    );

    RUN_TEST(
        test_empty_command_is_rejected
    );

    RUN_TEST(
        test_escaped_nul_is_rejected
    );

    UNITY_END();
}
