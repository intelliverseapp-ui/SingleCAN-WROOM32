#include "singlecan_commands.h"
#include "singlecan_command_dispatch.h"
#include "singlecan_command_handlers.h"
#include "singlecan_responses.h"

#include "bt_spp.h"
#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

static const char *TAG =
    "SingleCAN_CMDS";

#define COMMAND_FIELD_ID "id"
#define COMMAND_FIELD_TYPE "type"
#define COMMAND_FIELD_COMMAND "command"
#define COMMAND_FIELD_VALUE "value"

#define COMMAND_TYPE "command"
#define MODULE_CONFIG_COMMAND "config.module"

#define MAXIMUM_COMMAND_LENGTH 64
#define MAXIMUM_VALUE_LENGTH 64

#define REQUEST_RATE_WINDOW_MS 1000
#define REQUEST_RATE_MAXIMUM_COUNT 20

/*
 * All command state applies only to the current Bluetooth session.
 *
 * bt_spp.c calls singlecan_commands_reset_session() during
 * initialization, connection, and disconnection.
 */
static bool s_single_can_configured =
    false;

static bool s_request_id_history_valid =
    false;

static int s_highest_request_id =
    -1;

static TickType_t s_request_rate_window_start =
    0;

static uint32_t s_request_rate_count =
    0;

static bool s_request_rate_limit_logged =
    false;

// ------------------------------------------------------------
// FORWARD DECLARATIONS
// ------------------------------------------------------------

static bool parse_packet_id(
    const cJSON *id_item,
    int *packet_id
);

static bool process_module_config(
    const char *value
);

static void process_json_packet(
    const char *packet
);

static bool request_rate_limit_allows_packet(void);

// ------------------------------------------------------------
// PUBLIC SESSION STATE
// ------------------------------------------------------------

void singlecan_commands_reset_session(void)
{
    s_single_can_configured =
        false;

    s_request_id_history_valid =
        false;

    s_highest_request_id =
        -1;

    s_request_rate_window_start =
        0;

    s_request_rate_count =
        0;

    s_request_rate_limit_logged =
        false;

    ESP_LOGI(
        TAG,
        "Command session reset; configuration, request-ID "
        "history, and rate-limit state cleared"
    );
}

int singlecan_commands_is_configured(void)
{
    return s_single_can_configured
        ? 1
        : 0;
}

// ------------------------------------------------------------
// STRING VALIDATION
// ------------------------------------------------------------

static bool string_is_safe(
    const char *text,
    size_t maximum_length
)
{
    if (
        text == NULL ||
        text[0] == '\0'
    ) {
        return false;
    }

    const size_t length =
        strlen(
            text
        );

    if (
        length == 0 ||
        length >
        maximum_length
    ) {
        return false;
    }

    for (
        size_t index = 0;
        index < length;
        ++index
    ) {
        const unsigned char character =
            (unsigned char)text[index];

        if (
            character < 0x20U ||
            character == 0x7FU
        ) {
            return false;
        }
    }

    return true;
}

static bool packet_contains_escaped_nul(
    const char *packet
)
{
    if (packet == NULL) {
        return false;
    }

    const size_t length =
        strlen(
            packet
        );

    for (
        size_t index = 0;
        index + 5 < length;
        ++index
    ) {
        if (
            packet[index] == '\\' &&
            tolower(
                (unsigned char)packet[index + 1]
            ) == 'u' &&
            packet[index + 2] == '0' &&
            packet[index + 3] == '0' &&
            packet[index + 4] == '0' &&
            packet[index + 5] == '0'
        ) {
            return true;
        }
    }

    return false;
}

// ------------------------------------------------------------
// STRICT JSON FIELD VALIDATION
// ------------------------------------------------------------

static bool field_name_is_allowed(
    const char *field_name
)
{
    if (field_name == NULL) {
        return false;
    }

    return
        strcmp(
            field_name,
            COMMAND_FIELD_ID
        ) == 0 ||
        strcmp(
            field_name,
            COMMAND_FIELD_TYPE
        ) == 0 ||
        strcmp(
            field_name,
            COMMAND_FIELD_COMMAND
        ) == 0 ||
        strcmp(
            field_name,
            COMMAND_FIELD_VALUE
        ) == 0;
}

static bool validate_object_schema(
    const cJSON *root
)
{
    unsigned int id_count =
        0;

    unsigned int type_count =
        0;

    unsigned int command_count =
        0;

    unsigned int value_count =
        0;

    const cJSON *field =
        NULL;

    cJSON_ArrayForEach(
        field,
        root
    ) {
        if (
            field->string ==
            NULL
        ) {
            ESP_LOGW(
                TAG,
                "JSON object contains an unnamed field"
            );

            return false;
        }

        if (
            !field_name_is_allowed(
                field->string
            )
        ) {
            ESP_LOGW(
                TAG,
                "JSON object contains an unknown field"
            );

            return false;
        }

        if (
            strcmp(
                field->string,
                COMMAND_FIELD_ID
            ) == 0
        ) {
            id_count +=
                1;
        } else if (
            strcmp(
                field->string,
                COMMAND_FIELD_TYPE
            ) == 0
        ) {
            type_count +=
                1;
        } else if (
            strcmp(
                field->string,
                COMMAND_FIELD_COMMAND
            ) == 0
        ) {
            command_count +=
                1;
        } else if (
            strcmp(
                field->string,
                COMMAND_FIELD_VALUE
            ) == 0
        ) {
            value_count +=
                1;
        }
    }

    if (
        id_count != 1 ||
        type_count != 1 ||
        command_count != 1 ||
        value_count > 1
    ) {
        ESP_LOGW(
            TAG,
            "JSON object contains missing or duplicate fields"
        );

        return false;
    }

    return true;
}

// ------------------------------------------------------------
// STRICT REQUEST-ID VALIDATION
// ------------------------------------------------------------

static bool parse_packet_id(
    const cJSON *id_item,
    int *packet_id
)
{
    if (
        id_item == NULL ||
        packet_id == NULL ||
        !cJSON_IsNumber(
            id_item
        )
    ) {
        return false;
    }

    const double numeric_id =
        id_item->valuedouble;

    if (
        !isfinite(
            numeric_id
        ) ||
        numeric_id < 0.0 ||
        numeric_id > (double)INT_MAX ||
        floor(
            numeric_id
        ) != numeric_id
    ) {
        return false;
    }

    *packet_id =
        (int)numeric_id;

    return true;
}

// ------------------------------------------------------------
// REQUEST-ID REPLAY PROTECTION
// ------------------------------------------------------------

static bool request_id_is_fresh(
    int packet_id
)
{
    if (!s_request_id_history_valid) {
        return true;
    }

    return
        packet_id >
        s_highest_request_id;
}

static void record_request_id(
    int packet_id
)
{
    s_highest_request_id =
        packet_id;

    s_request_id_history_valid =
        true;
}

// ------------------------------------------------------------
// SINGLE-CAN MODULE CONFIGURATION
// ------------------------------------------------------------

static bool process_module_config(
    const char *value
)
{
    if (
        !string_is_safe(
            value,
            MAXIMUM_VALUE_LENGTH
        )
    ) {
        ESP_LOGW(
            TAG,
            "config.module received with an invalid value"
        );

        s_single_can_configured =
            false;

        return false;
    }

    if (
        strcasecmp(
            value,
            "single"
        ) == 0
    ) {
        s_single_can_configured =
            true;

        ESP_LOGI(
            TAG,
            "Module configured for this session: SINGLE_CAN"
        );

        return true;
    }

    if (
        strcasecmp(
            value,
            "dual"
        ) == 0
    ) {
        s_single_can_configured =
            false;

        ESP_LOGW(
            TAG,
            "Dual-CAN configuration rejected"
        );

        return false;
    }

    s_single_can_configured =
        false;

    ESP_LOGW(
        TAG,
        "Unknown module configuration rejected"
    );

    return false;
}

// ------------------------------------------------------------
// PER-SESSION REQUEST RATE LIMIT
// ------------------------------------------------------------

static bool request_rate_limit_allows_packet(void)
{
    const TickType_t current_tick =
        xTaskGetTickCount();

    const TickType_t window_ticks =
        pdMS_TO_TICKS(
            REQUEST_RATE_WINDOW_MS
        );

    if (
        s_request_rate_window_start == 0 ||
        (
            current_tick -
            s_request_rate_window_start
        ) >=
            window_ticks
    ) {
        s_request_rate_window_start =
            current_tick;

        s_request_rate_count =
            0;

        s_request_rate_limit_logged =
            false;
    }

    if (
        s_request_rate_count >=
        REQUEST_RATE_MAXIMUM_COUNT
    ) {
        if (!s_request_rate_limit_logged) {
            ESP_LOGW(
                TAG,
                "Per-session request rate exceeded: "
                "maximum=%d requests per %d ms; "
                "excess frames will be dropped",
                REQUEST_RATE_MAXIMUM_COUNT,
                REQUEST_RATE_WINDOW_MS
            );

            s_request_rate_limit_logged =
                true;
        }

        return false;
    }

    s_request_rate_count +=
        1;

    return true;
}

// ------------------------------------------------------------
// PROCESS JSON COMMAND ENVELOPE
// ------------------------------------------------------------

static void process_json_packet(
    const char *packet
)
{
    if (
        !request_rate_limit_allows_packet()
    ) {
        return;
    }

    if (
        packet_contains_escaped_nul(
            packet
        )
    ) {
        ESP_LOGW(
            TAG,
            "JSON packet contains an escaped NUL character"
        );

        return;
    }

    const char *parse_end =
        NULL;

    cJSON *root =
        cJSON_ParseWithOpts(
            packet,
            &parse_end,
            true
        );

    if (root == NULL) {
        ESP_LOGW(
            TAG,
            "Invalid JSON command envelope"
        );

        return;
    }

    if (
        parse_end == NULL ||
        *parse_end != '\0'
    ) {
        ESP_LOGW(
            TAG,
            "JSON envelope contains trailing data"
        );

        cJSON_Delete(
            root
        );

        return;
    }

    if (!cJSON_IsObject(root)) {
        ESP_LOGW(
            TAG,
            "JSON root is not an object"
        );

        cJSON_Delete(
            root
        );

        return;
    }

    if (
        !validate_object_schema(
            root
        )
    ) {
        cJSON_Delete(
            root
        );

        return;
    }

    const cJSON *id_item =
        cJSON_GetObjectItemCaseSensitive(
            root,
            COMMAND_FIELD_ID
        );

    const cJSON *type_item =
        cJSON_GetObjectItemCaseSensitive(
            root,
            COMMAND_FIELD_TYPE
        );

    const cJSON *command_item =
        cJSON_GetObjectItemCaseSensitive(
            root,
            COMMAND_FIELD_COMMAND
        );

    const cJSON *value_item =
        cJSON_GetObjectItemCaseSensitive(
            root,
            COMMAND_FIELD_VALUE
        );

    int packet_id =
        -1;

    if (
        !parse_packet_id(
            id_item,
            &packet_id
        )
    ) {
        ESP_LOGW(
            TAG,
            "JSON packet contains an invalid id"
        );

        cJSON_Delete(
            root
        );

        return;
    }

    if (
        !cJSON_IsString(
            type_item
        ) ||
        !string_is_safe(
            type_item->valuestring,
            sizeof(COMMAND_TYPE) - 1
        ) ||
        strcmp(
            type_item->valuestring,
            COMMAND_TYPE
        ) != 0
    ) {
        ESP_LOGW(
            TAG,
            "JSON packet contains an invalid type"
        );

        cJSON_Delete(
            root
        );

        return;
    }

    if (
        !cJSON_IsString(
            command_item
        ) ||
        !string_is_safe(
            command_item->valuestring,
            MAXIMUM_COMMAND_LENGTH
        )
    ) {
        ESP_LOGW(
            TAG,
            "JSON packet contains an invalid command"
        );

        cJSON_Delete(
            root
        );

        return;
    }

    const char *command =
        command_item->valuestring;

    const char *value =
        NULL;

    if (value_item != NULL) {
        if (
            !cJSON_IsString(
                value_item
            ) ||
            !string_is_safe(
                value_item->valuestring,
                MAXIMUM_VALUE_LENGTH
            )
        ) {
            ESP_LOGW(
                TAG,
                "JSON packet contains an invalid value"
            );

            singlecan_response_send(
                packet_id,
                "error",
                command,
                "invalid_value"
            );

            cJSON_Delete(
                root
            );

            return;
        }

        value =
            value_item->valuestring;
    }

    const bool is_module_config =
        strcasecmp(
            command,
            MODULE_CONFIG_COMMAND
        ) == 0;

    if (
        is_module_config &&
        value == NULL
    ) {
        singlecan_response_send(
            packet_id,
            "error",
            command,
            "missing_value"
        );

        cJSON_Delete(
            root
        );

        return;
    }

    if (
        !is_module_config &&
        value != NULL
    ) {
        singlecan_response_send(
            packet_id,
            "error",
            command,
            "unexpected_value"
        );

        cJSON_Delete(
            root
        );

        return;
    }

    /*
     * The packet has now passed structural schema, type, command,
     * value, and command-specific field validation.
     *
     * Reject duplicate, replayed, or out-of-order IDs before any
     * configuration change or vehicle-command dispatch occurs.
     */
    if (
        !request_id_is_fresh(
            packet_id
        )
    ) {
        ESP_LOGW(
            TAG,
            "Duplicate, replayed, or out-of-order request ID rejected"
        );

        singlecan_response_send(
            packet_id,
            "error",
            command,
            "duplicate_request_id"
        );

        cJSON_Delete(
            root
        );

        return;
    }

    /*
     * Record the ID before command processing.
     *
     * This prevents an accepted malformed configuration value,
     * unknown command, or currently unsupported command from being
     * replayed under the same ID.
     */
    record_request_id(
        packet_id
    );

    ESP_LOGI(
        TAG,
        "Validated fresh command envelope: id=%d",
        packet_id
    );

    if (is_module_config) {
        if (
            process_module_config(
                value
            )
        ) {
            singlecan_response_send(
                packet_id,
                "ok",
                command,
                NULL
            );
        } else {
            const char *reason =
                value != NULL &&
                strcasecmp(
                    value,
                    "dual"
                ) == 0
                    ? "dual_module_not_supported"
                    : "invalid_module";

            singlecan_response_send(
                packet_id,
                "error",
                command,
                reason
            );
        }

        cJSON_Delete(
            root
        );

        return;
    }

    if (!s_single_can_configured) {
        ESP_LOGW(
            TAG,
            "Vehicle command rejected before configuration"
        );

        singlecan_response_send(
            packet_id,
            "error",
            command,
            "module_not_configured"
        );

        cJSON_Delete(
            root
        );

        return;
    }

    const singlecan_command_dispatch_result_t command_result =
        singlecan_command_dispatch(
            command
        );

    if (
        command_result ==
        SINGLECAN_COMMAND_DISPATCH_NOT_IMPLEMENTED
    ) {
        singlecan_response_send(
            packet_id,
            "unsupported",
            command,
            "not_implemented"
        );
    } else {
        singlecan_response_send(
            packet_id,
            "unsupported",
            command,
            "unknown_command"
        );
    }

    cJSON_Delete(
        root
    );
}

// ------------------------------------------------------------
// PUBLIC COMMAND PROCESSOR
// ------------------------------------------------------------

void singlecan_commands_process(
    const char *packet
)
{
    if (packet == NULL) {
        ESP_LOGE(
            TAG,
            "NULL packet pointer"
        );

        return;
    }

    if (packet[0] == '\0') {
        ESP_LOGW(
            TAG,
            "Empty packet received"
        );

        return;
    }

    process_json_packet(
        packet
    );
}

