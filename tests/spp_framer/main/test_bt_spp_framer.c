#include "bt_spp_framer.h"

#include "unity.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TEST_MAX_FRAME_LENGTH 4096
#define CAPTURED_FRAME_CAPACITY 4

static char s_captured_frames[
    CAPTURED_FRAME_CAPACITY
][
    TEST_MAX_FRAME_LENGTH + 1
];

static size_t s_captured_count =
    0;

static void capture_frame(
    const char *frame
)
{
    if (
        frame == NULL ||
        s_captured_count >=
            CAPTURED_FRAME_CAPACITY
    ) {
        return;
    }

    strncpy(
        s_captured_frames[
            s_captured_count
        ],
        frame,
        TEST_MAX_FRAME_LENGTH
    );

    s_captured_frames[
        s_captured_count
    ][
        TEST_MAX_FRAME_LENGTH
    ] = '\0';

    s_captured_count +=
        1;
}

static void reset_capture(void)
{
    memset(
        s_captured_frames,
        0,
        sizeof(s_captured_frames)
    );

    s_captured_count =
        0;

    bt_spp_framer_reset();
}

static void process_text(
    const char *text
)
{
    bt_spp_framer_process(
        (const uint8_t *)text,
        strlen(text)
    );
}

static void test_fragmented_frame(void)
{
    reset_capture();

    process_text(
        "{\"id\":1,"
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_captured_count
    );

    process_text(
        "\"command\":\"LOCK_DOORS\"}\n"
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_captured_count
    );

    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":1,\"command\":\"LOCK_DOORS\"}",
        s_captured_frames[0]
    );
}

static void test_coalesced_frames(void)
{
    reset_capture();

    process_text(
        "first\nsecond\n"
    );

    TEST_ASSERT_EQUAL_UINT32(
        2,
        s_captured_count
    );

    TEST_ASSERT_EQUAL_STRING(
        "first",
        s_captured_frames[0]
    );

    TEST_ASSERT_EQUAL_STRING(
        "second",
        s_captured_frames[1]
    );
}

static void test_crlf_frame(void)
{
    reset_capture();

    process_text(
        "frame\r\n"
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_captured_count
    );

    TEST_ASSERT_EQUAL_STRING(
        "frame",
        s_captured_frames[0]
    );
}

static void test_fragmented_crlf_frame(void)
{
    reset_capture();

    process_text(
        "frame\r"
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_captured_count
    );

    process_text(
        "\n"
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_captured_count
    );

    TEST_ASSERT_EQUAL_STRING(
        "frame",
        s_captured_frames[0]
    );
}

static void test_empty_lines_are_ignored(void)
{
    reset_capture();

    process_text(
        "\n\r\n"
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_captured_count
    );
}

static void test_invalid_carriage_return_is_discarded(void)
{
    reset_capture();

    process_text(
        "bad\rcarriage\nvalid\n"
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_captured_count
    );

    TEST_ASSERT_EQUAL_STRING(
        "valid",
        s_captured_frames[0]
    );
}

static void test_control_byte_is_discarded(void)
{
    reset_capture();

    const uint8_t input[] = {
        'b',
        'a',
        'd',
        0x01,
        'x',
        '\n',
        'o',
        'k',
        '\n'
    };

    bt_spp_framer_process(
        input,
        sizeof(input)
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_captured_count
    );

    TEST_ASSERT_EQUAL_STRING(
        "ok",
        s_captured_frames[0]
    );
}

static void test_null_and_empty_fragments_are_ignored(void)
{
    reset_capture();

    bt_spp_framer_process(
        NULL,
        5
    );

    bt_spp_framer_process(
        (const uint8_t *)"ignored",
        0
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        s_captured_count
    );
}

static void test_reset_drops_partial_frame(void)
{
    reset_capture();

    process_text(
        "old-session-data"
    );

    bt_spp_framer_reset();

    process_text(
        "new-session\n"
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_captured_count
    );

    TEST_ASSERT_EQUAL_STRING(
        "new-session",
        s_captured_frames[0]
    );
}

static void test_maximum_length_frame_is_accepted(void)
{
    reset_capture();

    char *input =
        malloc(
            TEST_MAX_FRAME_LENGTH + 2
        );

    TEST_ASSERT_NOT_NULL(
        input
    );

    memset(
        input,
        'A',
        TEST_MAX_FRAME_LENGTH
    );

    input[
        TEST_MAX_FRAME_LENGTH
    ] = '\n';

    input[
        TEST_MAX_FRAME_LENGTH + 1
    ] = '\0';

    bt_spp_framer_process(
        (const uint8_t *)input,
        TEST_MAX_FRAME_LENGTH + 1
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_captured_count
    );

    TEST_ASSERT_EQUAL_UINT32(
        TEST_MAX_FRAME_LENGTH,
        strlen(
            s_captured_frames[0]
        )
    );

    free(
        input
    );
}

static void test_oversized_frame_is_discarded_and_recovers(void)
{
    reset_capture();

    char *input =
        malloc(
            TEST_MAX_FRAME_LENGTH + 5
        );

    TEST_ASSERT_NOT_NULL(
        input
    );

    memset(
        input,
        'B',
        TEST_MAX_FRAME_LENGTH + 1
    );

    input[
        TEST_MAX_FRAME_LENGTH + 1
    ] = '\n';

    input[
        TEST_MAX_FRAME_LENGTH + 2
    ] = 'o';

    input[
        TEST_MAX_FRAME_LENGTH + 3
    ] = 'k';

    input[
        TEST_MAX_FRAME_LENGTH + 4
    ] = '\n';

    bt_spp_framer_process(
        (const uint8_t *)input,
        TEST_MAX_FRAME_LENGTH + 5
    );

    TEST_ASSERT_EQUAL_UINT32(
        1,
        s_captured_count
    );

    TEST_ASSERT_EQUAL_STRING(
        "ok",
        s_captured_frames[0]
    );

    free(
        input
    );
}

void app_main(void)
{
    TEST_ASSERT_TRUE(
        bt_spp_framer_init(
            capture_frame
        )
    );

    UNITY_BEGIN();

    RUN_TEST(
        test_fragmented_frame
    );

    RUN_TEST(
        test_coalesced_frames
    );

    RUN_TEST(
        test_crlf_frame
    );

    RUN_TEST(
        test_fragmented_crlf_frame
    );

    RUN_TEST(
        test_empty_lines_are_ignored
    );

    RUN_TEST(
        test_invalid_carriage_return_is_discarded
    );

    RUN_TEST(
        test_control_byte_is_discarded
    );

    RUN_TEST(
        test_null_and_empty_fragments_are_ignored
    );

    RUN_TEST(
        test_reset_drops_partial_frame
    );

    RUN_TEST(
        test_maximum_length_frame_is_accepted
    );

    RUN_TEST(
        test_oversized_frame_is_discarded_and_recovers
    );

    UNITY_END();
}
