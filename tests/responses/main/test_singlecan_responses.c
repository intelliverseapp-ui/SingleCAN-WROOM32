#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "singlecan_responses.h"
#include "unity.h"

static char s_sent_message[512];

esp_err_t bt_spp_send(
    const char *message
)
{
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

void app_main(void)
{
    UNITY_BEGIN();

    RUN_TEST(
        test_response_contains_protocol_version
    );

    UNITY_END();
}
