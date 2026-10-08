#include "singlecan_command_dispatch.h"

#include "unity.h"

#include <string.h>

// ------------------------------------------------------------
// TEST RUNNER
// ------------------------------------------------------------

static void test_recognized_command(void)
{
    const singlecan_command_dispatch_result_t result =
        singlecan_command_dispatch(
            "LOCK_DOORS"
        );

    TEST_ASSERT_EQUAL_INT(
        SINGLECAN_COMMAND_DISPATCH_NOT_IMPLEMENTED,
        result
    );
}

static void test_recognized_alias(void)
{
    const singlecan_command_dispatch_result_t result =
        singlecan_command_dispatch(
            "locks.all.lock"
        );

    TEST_ASSERT_EQUAL_INT(
        SINGLECAN_COMMAND_DISPATCH_NOT_IMPLEMENTED,
        result
    );
}

static void test_unknown_command(void)
{
    const singlecan_command_dispatch_result_t result =
        singlecan_command_dispatch(
            "UNVERIFIED_ARBITRARY_COMMAND"
        );

    TEST_ASSERT_EQUAL_INT(
        SINGLECAN_COMMAND_DISPATCH_UNSUPPORTED,
        result
    );
}

static void test_null_command(void)
{
    const singlecan_command_dispatch_result_t result =
        singlecan_command_dispatch(
            NULL
        );

    TEST_ASSERT_EQUAL_INT(
        SINGLECAN_COMMAND_DISPATCH_UNSUPPORTED,
        result
    );
}

static void test_empty_command(void)
{
    const singlecan_command_dispatch_result_t result =
        singlecan_command_dispatch(
            ""
        );

    TEST_ASSERT_EQUAL_INT(
        SINGLECAN_COMMAND_DISPATCH_UNSUPPORTED,
        result
    );
}

static void test_overlength_command(void)
{
    char command[66];

    memset(
        command,
        'A',
        sizeof(command) - 1
    );

    command[
        sizeof(command) - 1
    ] = '\0';

    const singlecan_command_dispatch_result_t result =
        singlecan_command_dispatch(
            command
        );

    TEST_ASSERT_EQUAL_INT(
        SINGLECAN_COMMAND_DISPATCH_UNSUPPORTED,
        result
    );
}

static void test_control_character_command(void)
{
    const char command[] = {
        'L',
        'O',
        'C',
        'K',
        '\n',
        '\0'
    };

    const singlecan_command_dispatch_result_t result =
        singlecan_command_dispatch(
            command
        );

    TEST_ASSERT_EQUAL_INT(
        SINGLECAN_COMMAND_DISPATCH_UNSUPPORTED,
        result
    );
}

void app_main(void)
{
    UNITY_BEGIN();

    RUN_TEST(
        test_recognized_command
    );

    RUN_TEST(
        test_recognized_alias
    );

    RUN_TEST(
        test_unknown_command
    );

    RUN_TEST(
        test_null_command
    );

    RUN_TEST(
        test_empty_command
    );

    RUN_TEST(
        test_overlength_command
    );

    RUN_TEST(
        test_control_character_command
    );

    UNITY_END();
}
