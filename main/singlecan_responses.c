#include "singlecan_responses.h"

#include "bt_spp.h"
#include "cJSON.h"
#include "esp_log.h"

static const char *TAG =
    "SingleCAN_RESPONSES";

// ------------------------------------------------------------
// JSON PROTOCOL RESPONSE
// ------------------------------------------------------------

esp_err_t singlecan_response_send(
    int packet_id,
    const char *status,
    const char *command,
    const char *reason
)
{
    if (
        packet_id < 0 ||
        status == NULL ||
        status[0] == '\0' ||
        command == NULL ||
        command[0] == '\0'
    ) {
        ESP_LOGE(
            TAG,
            "Cannot create response with invalid fields"
        );

        return ESP_ERR_INVALID_ARG;
    }

    cJSON *response =
        cJSON_CreateObject();

    if (response == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create response JSON object"
        );

        return ESP_ERR_NO_MEM;
    }

    int valid =
        1;

    if (
        cJSON_AddNumberToObject(
            response,
            "id",
            packet_id
        ) == NULL
    ) {
        valid =
            0;
    }

    if (
        valid &&
        cJSON_AddStringToObject(
            response,
            "type",
            "response"
        ) == NULL
    ) {
        valid =
            0;
    }

    if (
        valid &&
        cJSON_AddStringToObject(
            response,
            "status",
            status
        ) == NULL
    ) {
        valid =
            0;
    }

    if (
        valid &&
        cJSON_AddStringToObject(
            response,
            "command",
            command
        ) == NULL
    ) {
        valid =
            0;
    }

    if (
        valid &&
        reason != NULL &&
        reason[0] != '\0' &&
        cJSON_AddStringToObject(
            response,
            "reason",
            reason
        ) == NULL
    ) {
        valid =
            0;
    }

    if (!valid) {
        ESP_LOGE(
            TAG,
            "Failed to construct response JSON"
        );

        cJSON_Delete(
            response
        );

        return ESP_ERR_NO_MEM;
    }

    char *response_text =
        cJSON_PrintUnformatted(
            response
        );

    if (response_text == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to serialize response JSON"
        );

        cJSON_Delete(
            response
        );

        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "Sending response: id=%d status=%s",
        packet_id,
        status
    );

    const esp_err_t send_result =
        bt_spp_send(
            response_text
        );

    if (
        send_result !=
        ESP_OK
    ) {
        ESP_LOGE(
            TAG,
            "Failed to queue response: %s",
            esp_err_to_name(
                send_result
            )
        );
    }

    cJSON_free(
        response_text
    );

    cJSON_Delete(
        response
    );

    return send_result;
}
