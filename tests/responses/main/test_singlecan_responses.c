#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "singlecan_responses.h"
#include "unity.h"

static char s_sent_message[512];

esp_err_t bt_spp_send_for_session(
    const char *message,
    uint32_t session_id
)
{
    if (session_id == 0U) {
        return ESP_ERR_INVALID_STATE;
    }

    if (message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(
        s_sent_message,
        message,
        sizeof(s_sent_message) - 1
    );

    s_sent_message[
        sizeof(s_sent_message) - 1
    ] = '\0';

    return ESP_OK;
}

static void test_response_contains_protocol_version(void)
{
    memset(
        s_sent_message,
        0,
        sizeof(s_sent_message)
    );

    TEST_ASSERT_EQUAL(
        ESP_OK,
        singlecan_response_send(
            1,
            42U,
            "ok",
            "config.module",
            NULL
        )
    );

    cJSON *response =
        cJSON_Parse(
            s_sent_message
        );

    TEST_ASSERT_NOT_NULL(
        response
    );

    const cJSON *version =
        cJSON_GetObjectItemCaseSensitive(
            response,
            "version"
        );

    TEST_ASSERT_TRUE(
        cJSON_IsNumber(
            version
        )
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        version->valueint
    );

    cJSON_Delete(
        response
    );
}


static void test_response_contains_originating_session_id(void)
{
    memset(
        s_sent_message,
        0,
        sizeof(s_sent_message)
    );

    TEST_ASSERT_EQUAL(
        ESP_OK,
        singlecan_response_send(
            7,
            42U,
            "ok",
            "config.module",
            NULL
        )
    );

    cJSON *response =
        cJSON_Parse(
            s_sent_message
        );

    TEST_ASSERT_NOT_NULL(
        response
    );

    TEST_ASSERT_EQUAL_UINT32(
        42U,
        (uint32_t)cJSON_GetObjectItemCaseSensitive(
            response,
            "session_id"
        )->valuedouble
    );

    cJSON_Delete(
        response
    );
}

static void test_response_rejects_zero_session_id(void)
{
    memset(
        s_sent_message,
        0,
        sizeof(s_sent_message)
    );

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        singlecan_response_send(
            7,
            0U,
            "ok",
            "config.module",
            NULL
        )
    );

    TEST_ASSERT_EQUAL_CHAR(
        '\0',
        s_sent_message[0]
    );
}

static void test_handshake_response_contains_session_and_capabilities(void)
{
    memset(
        s_sent_message,
        0,
        sizeof(s_sent_message)
    );

    TEST_ASSERT_EQUAL(
        ESP_OK,
        singlecan_handshake_response_send(
            1,
            42U
        )
    );

    cJSON *response =
        cJSON_Parse(
            s_sent_message
        );

    TEST_ASSERT_NOT_NULL(
        response
    );

    TEST_ASSERT_EQUAL_STRING(
        "response",
        cJSON_GetObjectItemCaseSensitive(
            response,
            "type"
        )->valuestring
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        cJSON_GetObjectItemCaseSensitive(
            response,
            "version"
        )->valueint
    );

    TEST_ASSERT_EQUAL_INT(
        42,
        cJSON_GetObjectItemCaseSensitive(
            response,
            "session_id"
        )->valueint
    );

    TEST_ASSERT_EQUAL_STRING(
        "single",
        cJSON_GetObjectItemCaseSensitive(
            response,
            "module"
        )->valuestring
    );

    TEST_ASSERT_TRUE(
        cJSON_IsFalse(
            cJSON_GetObjectItemCaseSensitive(
                response,
                "can_transmit"
            )
        )
    );

    TEST_ASSERT_EQUAL_STRING(
        "stub_only",
        cJSON_GetObjectItemCaseSensitive(
            response,
            "vehicle_commands"
        )->valuestring
    );

    cJSON_Delete(
        response
    );
}

void app_main(void)
{
    UNITY_BEGIN();

    RUN_TEST(
        test_response_contains_protocol_version
    );

    RUN_TEST(
        test_response_contains_originating_session_id
    );

    RUN_TEST(
        test_response_rejects_zero_session_id
    );

    RUN_TEST(
        test_handshake_response_contains_session_and_capabilities
    );

    UNITY_END();
}
