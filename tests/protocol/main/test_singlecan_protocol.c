#include "protocol_test_doubles.h"
#include "singlecan_commands.h"

#include "unity.h"

#include <stdio.h>

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


static void test_vehicle_command_before_configuration_is_blocked(void)
{
    protocol_test_doubles_reset();
    singlecan_commands_reset_session();

    singlecan_commands_process(
        "{\"id\":1,\"type\":\"command\","
        "\"command\":\"LOCK_DOORS\"}"
    );

    const protocol_test_response_capture_t *response =
        protocol_test_response_capture();

    TEST_ASSERT_EQUAL_INT(0, singlecan_commands_is_configured());
    TEST_ASSERT_EQUAL_INT(1, response->call_count);
    TEST_ASSERT_EQUAL_STRING("error", response->status);
    TEST_ASSERT_EQUAL_STRING(
        "module_not_configured",
        response->reason
    );
    TEST_ASSERT_EQUAL_INT(
        0,
        protocol_test_dispatch_call_count()
    );
}

static void test_dual_module_configuration_is_rejected(void)
{
    protocol_test_doubles_reset();
    singlecan_commands_reset_session();

    singlecan_commands_process(
        "{\"id\":1,\"type\":\"command\","
        "\"command\":\"config.module\","
        "\"value\":\"dual\"}"
    );

    const protocol_test_response_capture_t *response =
        protocol_test_response_capture();

    TEST_ASSERT_EQUAL_INT(0, singlecan_commands_is_configured());
    TEST_ASSERT_EQUAL_INT(1, response->call_count);
    TEST_ASSERT_EQUAL_STRING("error", response->status);
    TEST_ASSERT_EQUAL_STRING(
        "dual_module_not_supported",
        response->reason
    );
}

static void test_unknown_module_configuration_is_rejected(void)
{
    protocol_test_doubles_reset();
    singlecan_commands_reset_session();

    singlecan_commands_process(
        "{\"id\":1,\"type\":\"command\","
        "\"command\":\"config.module\","
        "\"value\":\"unknown\"}"
    );

    const protocol_test_response_capture_t *response =
        protocol_test_response_capture();

    TEST_ASSERT_EQUAL_INT(0, singlecan_commands_is_configured());
    TEST_ASSERT_EQUAL_STRING("error", response->status);
    TEST_ASSERT_EQUAL_STRING("invalid_module", response->reason);
}

static void test_config_missing_value_returns_error(void)
{
    protocol_test_doubles_reset();
    singlecan_commands_reset_session();

    singlecan_commands_process(
        "{\"id\":1,\"type\":\"command\","
        "\"command\":\"config.module\"}"
    );

    const protocol_test_response_capture_t *response =
        protocol_test_response_capture();

    TEST_ASSERT_EQUAL_INT(1, response->call_count);
    TEST_ASSERT_EQUAL_STRING("error", response->status);
    TEST_ASSERT_EQUAL_STRING("missing_value", response->reason);
    TEST_ASSERT_EQUAL_INT(0, singlecan_commands_is_configured());
}

static void test_vehicle_command_with_value_returns_error(void)
{
    protocol_test_doubles_reset();
    singlecan_commands_reset_session();

    singlecan_commands_process(
        "{\"id\":1,\"type\":\"command\","
        "\"command\":\"LOCK_DOORS\","
        "\"value\":\"unsafe\"}"
    );

    const protocol_test_response_capture_t *response =
        protocol_test_response_capture();

    TEST_ASSERT_EQUAL_INT(1, response->call_count);
    TEST_ASSERT_EQUAL_STRING("error", response->status);
    TEST_ASSERT_EQUAL_STRING("unexpected_value", response->reason);
    TEST_ASSERT_EQUAL_INT(
        0,
        protocol_test_dispatch_call_count()
    );
}


static void test_recognized_command_dispatches_after_configuration(void)
{
    protocol_test_doubles_reset();
    singlecan_commands_reset_session();

    singlecan_commands_process(
        "{\"id\":1,\"type\":\"command\","
        "\"command\":\"config.module\","
        "\"value\":\"single\"}"
    );

    singlecan_commands_process(
        "{\"id\":2,\"type\":\"command\","
        "\"command\":\"LOCK_DOORS\"}"
    );

    const protocol_test_response_capture_t *response =
        protocol_test_response_capture();

    TEST_ASSERT_EQUAL_INT(
        1,
        singlecan_commands_is_configured()
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        protocol_test_dispatch_call_count()
    );

    TEST_ASSERT_EQUAL_STRING(
        "LOCK_DOORS",
        protocol_test_dispatched_command()
    );

    TEST_ASSERT_EQUAL_INT(
        2,
        response->packet_id
    );

    TEST_ASSERT_EQUAL_STRING(
        "unsupported",
        response->status
    );

    TEST_ASSERT_EQUAL_STRING(
        "not_implemented",
        response->reason
    );
}

static void test_unknown_command_returns_unknown_command(void)
{
    protocol_test_doubles_reset();
    singlecan_commands_reset_session();

    singlecan_commands_process(
        "{\"id\":1,\"type\":\"command\","
        "\"command\":\"config.module\","
        "\"value\":\"single\"}"
    );

    protocol_test_set_dispatch_result(
        SINGLECAN_COMMAND_DISPATCH_UNSUPPORTED
    );

    singlecan_commands_process(
        "{\"id\":2,\"type\":\"command\","
        "\"command\":\"UNKNOWN_COMMAND\"}"
    );

    const protocol_test_response_capture_t *response =
        protocol_test_response_capture();

    TEST_ASSERT_EQUAL_INT(
        1,
        protocol_test_dispatch_call_count()
    );

    TEST_ASSERT_EQUAL_STRING(
        "UNKNOWN_COMMAND",
        protocol_test_dispatched_command()
    );

    TEST_ASSERT_EQUAL_STRING(
        "unsupported",
        response->status
    );

    TEST_ASSERT_EQUAL_STRING(
        "unknown_command",
        response->reason
    );
}

static void test_duplicate_request_id_is_rejected(void)
{
    protocol_test_doubles_reset();
    singlecan_commands_reset_session();

    singlecan_commands_process(
        "{\"id\":1,\"type\":\"command\","
        "\"command\":\"config.module\","
        "\"value\":\"single\"}"
    );

    singlecan_commands_process(
        "{\"id\":2,\"type\":\"command\","
        "\"command\":\"LOCK_DOORS\"}"
    );

    singlecan_commands_process(
        "{\"id\":2,\"type\":\"command\","
        "\"command\":\"UNLOCK_DOORS\"}"
    );

    const protocol_test_response_capture_t *response =
        protocol_test_response_capture();

    TEST_ASSERT_EQUAL_INT(
        1,
        protocol_test_dispatch_call_count()
    );

    TEST_ASSERT_EQUAL_INT(
        2,
        response->packet_id
    );

    TEST_ASSERT_EQUAL_STRING(
        "error",
        response->status
    );

    TEST_ASSERT_EQUAL_STRING(
        "duplicate_request_id",
        response->reason
    );
}


static void test_out_of_order_request_id_is_rejected(void)
{
    protocol_test_doubles_reset();
    singlecan_commands_reset_session();

    singlecan_commands_process(
        "{\"id\":10,\"type\":\"command\","
        "\"command\":\"config.module\","
        "\"value\":\"single\"}"
    );

    singlecan_commands_process(
        "{\"id\":9,\"type\":\"command\","
        "\"command\":\"LOCK_DOORS\"}"
    );

    const protocol_test_response_capture_t *response =
        protocol_test_response_capture();

    TEST_ASSERT_EQUAL_INT(
        0,
        protocol_test_dispatch_call_count()
    );

    TEST_ASSERT_EQUAL_INT(
        9,
        response->packet_id
    );

    TEST_ASSERT_EQUAL_STRING(
        "error",
        response->status
    );

    TEST_ASSERT_EQUAL_STRING(
        "duplicate_request_id",
        response->reason
    );
}

static void test_session_reset_clears_configuration(void)
{
    protocol_test_doubles_reset();
    singlecan_commands_reset_session();

    singlecan_commands_process(
        "{\"id\":10,\"type\":\"command\","
        "\"command\":\"config.module\","
        "\"value\":\"single\"}"
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        singlecan_commands_is_configured()
    );

    protocol_test_doubles_reset();
    singlecan_commands_reset_session();

    TEST_ASSERT_EQUAL_INT(
        0,
        singlecan_commands_is_configured()
    );

    singlecan_commands_process(
        "{\"id\":11,\"type\":\"command\","
        "\"command\":\"LOCK_DOORS\"}"
    );

    const protocol_test_response_capture_t *response =
        protocol_test_response_capture();

    TEST_ASSERT_EQUAL_INT(
        0,
        protocol_test_dispatch_call_count()
    );

    TEST_ASSERT_EQUAL_STRING(
        "module_not_configured",
        response->reason
    );
}

static void test_session_reset_allows_request_ids_to_restart(void)
{
    protocol_test_doubles_reset();
    singlecan_commands_reset_session();

    singlecan_commands_process(
        "{\"id\":10,\"type\":\"command\","
        "\"command\":\"config.module\","
        "\"value\":\"single\"}"
    );

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
}


static void test_per_session_rate_limit_drops_excess_packet(void)
{
    protocol_test_doubles_reset();
    singlecan_commands_reset_session();

    singlecan_commands_process(
        "{\"id\":1,\"type\":\"command\","
        "\"command\":\"config.module\","
        "\"value\":\"single\"}"
    );

    /*
     * The configuration packet consumes one of the 20 allowed
     * requests. IDs 2 through 20 consume the remaining 19.
     */
    for (
        int packet_id = 2;
        packet_id <= 20;
        ++packet_id
    ) {
        char packet[128];

        snprintf(
            packet,
            sizeof(packet),
            "{\"id\":%d,\"type\":\"command\","
            "\"command\":\"LOCK_DOORS\"}",
            packet_id
        );

        singlecan_commands_process(
            packet
        );
    }

    const protocol_test_response_capture_t *response =
        protocol_test_response_capture();

    TEST_ASSERT_EQUAL_INT(
        20,
        response->call_count
    );

    TEST_ASSERT_EQUAL_INT(
        19,
        protocol_test_dispatch_call_count()
    );

    TEST_ASSERT_EQUAL_INT(
        20,
        response->packet_id
    );

    /*
     * Packet 21 exceeds the current per-session rate window.
     * It must produce neither a response nor another dispatch.
     */
    singlecan_commands_process(
        "{\"id\":21,\"type\":\"command\","
        "\"command\":\"UNLOCK_DOORS\"}"
    );

    TEST_ASSERT_EQUAL_INT(
        20,
        response->call_count
    );

    TEST_ASSERT_EQUAL_INT(
        19,
        protocol_test_dispatch_call_count()
    );

    TEST_ASSERT_EQUAL_INT(
        20,
        response->packet_id
    );

    TEST_ASSERT_EQUAL_STRING(
        "LOCK_DOORS",
        protocol_test_dispatched_command()
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


    RUN_TEST(
        test_vehicle_command_before_configuration_is_blocked
    );

    RUN_TEST(
        test_dual_module_configuration_is_rejected
    );

    RUN_TEST(
        test_unknown_module_configuration_is_rejected
    );

    RUN_TEST(
        test_config_missing_value_returns_error
    );

    RUN_TEST(
        test_vehicle_command_with_value_returns_error
    );


    RUN_TEST(
        test_recognized_command_dispatches_after_configuration
    );

    RUN_TEST(
        test_unknown_command_returns_unknown_command
    );

    RUN_TEST(
        test_duplicate_request_id_is_rejected
    );


    RUN_TEST(
        test_out_of_order_request_id_is_rejected
    );

    RUN_TEST(
        test_session_reset_clears_configuration
    );

    RUN_TEST(
        test_session_reset_allows_request_ids_to_restart
    );


    RUN_TEST(
        test_per_session_rate_limit_drops_excess_packet
    );

    UNITY_END();
}
