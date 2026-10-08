#include "singlecan_commands.h"

#include "bt_spp.h"
#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"

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

typedef enum {
    COMMAND_RESULT_NOT_IMPLEMENTED = 0,
    COMMAND_RESULT_UNSUPPORTED
} command_result_t;

typedef void (*command_handler_t)(void);

typedef struct {
    const char *command;
    command_handler_t handler;
} command_mapping_t;

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

// ------------------------------------------------------------
// FORWARD DECLARATIONS
// ------------------------------------------------------------

static void send_response(
    int packet_id,
    const char *status,
    const char *command,
    const char *reason
);

static bool parse_packet_id(
    const cJSON *id_item,
    int *packet_id
);

static bool process_module_config(
    const char *value
);

static command_result_t dispatch_command(
    const char *command
);

static void process_json_packet(
    const char *packet
);

// ------------------------------------------------------------
// COMMAND ALLOWLIST
// ------------------------------------------------------------

static const command_mapping_t COMMAND_MAPPINGS[] = {
    {
        "LOCK_DOORS",
        singlecan_cmd_lock_doors
    },
    {
        "locks.all.lock",
        singlecan_cmd_lock_doors
    },
    {
        "UNLOCK_DOORS",
        singlecan_cmd_unlock_doors
    },
    {
        "locks.all.unlock",
        singlecan_cmd_unlock_doors
    },
    {
        "WINDOW_DRIVER_DOWN",
        singlecan_cmd_windows_down
    },
    {
        "WINDOW_PASSENGER_DOWN",
        singlecan_cmd_windows_down
    },
    {
        "WINDOWS_DOWN",
        singlecan_cmd_windows_down
    },
    {
        "WINDOW_DRIVER_UP",
        singlecan_cmd_windows_up
    },
    {
        "WINDOW_PASSENGER_UP",
        singlecan_cmd_windows_up
    },
    {
        "WINDOWS_UP",
        singlecan_cmd_windows_up
    },
    {
        "SUNROOF_OPEN",
        singlecan_cmd_sunroof_open
    },
    {
        "MOONROOF_OPEN",
        singlecan_cmd_sunroof_open
    },
    {
        "SUNROOF_CLOSE",
        singlecan_cmd_sunroof_close
    },
    {
        "MOONROOF_CLOSE",
        singlecan_cmd_sunroof_close
    },
    {
        "SUNROOF_VENT",
        singlecan_cmd_sunroof_vent
    },
    {
        "HEADLIGHTS_ON",
        singlecan_cmd_headlights_on
    },
    {
        "HEADLIGHTS_OFF",
        singlecan_cmd_headlights_off
    },
    {
        "FOG_LIGHTS_ON",
        singlecan_cmd_fog_lights_on
    },
    {
        "FOG_LIGHTS_OFF",
        singlecan_cmd_fog_lights_off
    },
    {
        "INTERIOR_LIGHTS_ON",
        singlecan_cmd_interior_lights_on
    },
    {
        "INTERIOR_LIGHTS_OFF",
        singlecan_cmd_interior_lights_off
    },
    {
        "AC_ON",
        singlecan_cmd_ac_on
    },
    {
        "AC_OFF",
        singlecan_cmd_ac_off
    },
    {
        "FAN_UP",
        singlecan_cmd_fan_up
    },
    {
        "FAN_DOWN",
        singlecan_cmd_fan_down
    },
    {
        "AUDIO_MUTE",
        singlecan_cmd_audio_mute
    },
    {
        "AUDIO_UNMUTE",
        singlecan_cmd_audio_unmute
    },
    {
        "TRUNK_OPEN",
        singlecan_cmd_trunk_open
    },
    {
        "HORN",
        singlecan_cmd_horn
    },
    {
        "HORN_SHORT",
        singlecan_cmd_horn
    },
    {
        "HAZARDS_ON",
        singlecan_cmd_hazards_on
    },
    {
        "HAZARDS_OFF",
        singlecan_cmd_hazards_off
    },
    {
        "DEFROST_ON",
        singlecan_cmd_defrost_on
    },
    {
        "DEFROST_FRONT",
        singlecan_cmd_defrost_on
    },
    {
        "DEFROST_REAR",
        singlecan_cmd_defrost_on
    },
    {
        "DEFROST_OFF",
        singlecan_cmd_defrost_off
    },
    {
        "DEFROST_REAR_OFF",
        singlecan_cmd_defrost_off
    }
};

static const size_t COMMAND_MAPPING_COUNT =
    sizeof(COMMAND_MAPPINGS) /
    sizeof(COMMAND_MAPPINGS[0]);

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

    ESP_LOGI(
        TAG,
        "Command session reset; configuration and request-ID "
        "history cleared"
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
// SEND JSON RESPONSE
// ------------------------------------------------------------

static void send_response(
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

        return;
    }

    cJSON *response =
        cJSON_CreateObject();

    if (response == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create response JSON object"
        );

        return;
    }

    bool valid =
        true;

    if (
        cJSON_AddNumberToObject(
            response,
            "id",
            packet_id
        ) == NULL
    ) {
        valid =
            false;
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
            false;
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
            false;
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
            false;
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
            false;
    }

    if (!valid) {
        ESP_LOGE(
            TAG,
            "Failed to construct response JSON"
        );

        cJSON_Delete(
            response
        );

        return;
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

        return;
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
// CANONICAL COMMAND DISPATCHER
// ------------------------------------------------------------

static command_result_t dispatch_command(
    const char *command
)
{
    if (
        !string_is_safe(
            command,
            MAXIMUM_COMMAND_LENGTH
        )
    ) {
        return COMMAND_RESULT_UNSUPPORTED;
    }

    for (
        size_t index = 0;
        index < COMMAND_MAPPING_COUNT;
        ++index
    ) {
        if (
            strcasecmp(
                command,
                COMMAND_MAPPINGS[index].command
            ) == 0
        ) {
            ESP_LOGI(
                TAG,
                "Recognized allowlisted command"
            );

            COMMAND_MAPPINGS[index]
                .handler();

            return COMMAND_RESULT_NOT_IMPLEMENTED;
        }
    }

    ESP_LOGW(
        TAG,
        "Unsupported canonical command"
    );

    return COMMAND_RESULT_UNSUPPORTED;
}

// ------------------------------------------------------------
// PROCESS JSON COMMAND ENVELOPE
// ------------------------------------------------------------

static void process_json_packet(
    const char *packet
)
{
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

            send_response(
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
        send_response(
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
        send_response(
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

        send_response(
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
            send_response(
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

            send_response(
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

        send_response(
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

    const command_result_t command_result =
        dispatch_command(
            command
        );

    if (
        command_result ==
        COMMAND_RESULT_NOT_IMPLEMENTED
    ) {
        send_response(
            packet_id,
            "unsupported",
            command,
            "not_implemented"
        );
    } else {
        send_response(
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

// ------------------------------------------------------------
// HIGH-LEVEL VEHICLE COMMAND STUBS
// ------------------------------------------------------------
// These handlers intentionally do not send CAN frames.
// ------------------------------------------------------------

#define DEFINE_COMMAND_STUB(function_name, description) \
    void function_name(void) \
    { \
        ESP_LOGI( \
            TAG, \
            "CMD recognized: " description \
            "; mapping not implemented" \
        ); \
    }

DEFINE_COMMAND_STUB(
    singlecan_cmd_lock_doors,
    "lock doors"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_unlock_doors,
    "unlock doors"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_windows_down,
    "windows down"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_windows_up,
    "windows up"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_sunroof_open,
    "sunroof open"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_sunroof_close,
    "sunroof close"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_sunroof_vent,
    "sunroof vent"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_headlights_on,
    "headlights on"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_headlights_off,
    "headlights off"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_fog_lights_on,
    "fog lights on"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_fog_lights_off,
    "fog lights off"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_interior_lights_on,
    "interior lights on"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_interior_lights_off,
    "interior lights off"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_ac_on,
    "AC on"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_ac_off,
    "AC off"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_fan_up,
    "fan up"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_fan_down,
    "fan down"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_audio_mute,
    "audio mute"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_audio_unmute,
    "audio unmute"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_trunk_open,
    "trunk open"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_horn,
    "horn"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_hazards_on,
    "hazards on"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_hazards_off,
    "hazards off"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_defrost_on,
    "defrost on"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_defrost_off,
    "defrost off"
)

#undef DEFINE_COMMAND_STUB