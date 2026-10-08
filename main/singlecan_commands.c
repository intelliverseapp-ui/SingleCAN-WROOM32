#include "singlecan_commands.h"

#include "bt_spp.h"
#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

static const char *TAG =
    "SingleCAN_CMDS";

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
 * Configuration applies only to the current Bluetooth session.
 * bt_spp.c will reset this state when a client connects or
 * disconnects.
 */
static bool s_single_can_configured =
    false;

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

    ESP_LOGI(
        TAG,
        "Command session reset; module configuration required"
    );
}

int singlecan_commands_is_configured(void)
{
    return s_single_can_configured
        ? 1
        : 0;
}

// ------------------------------------------------------------
// SEND JSON RESPONSE TO BABYNODE AUTOMOTIVE
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
        "Sending response: id=%d status=%s command=%s",
        packet_id,
        status,
        command
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
// SINGLE-CAN MODULE CONFIGURATION
// ------------------------------------------------------------

static bool process_module_config(
    const char *value
)
{
    if (
        value == NULL ||
        value[0] == '\0'
    ) {
        ESP_LOGW(
            TAG,
            "config.module received without a value"
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

    /*
     * This firmware controls one CAN channel only. It must not
     * acknowledge Dual-CAN configuration that it cannot apply.
     */
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
            "Dual-CAN configuration rejected by SingleCAN firmware"
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
        command == NULL ||
        command[0] == '\0'
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
                "Recognized allowlisted command: %s",
                command
            );

            COMMAND_MAPPINGS[index]
                .handler();

            /*
             * Every current handler is still a non-transmitting
             * stub. Therefore recognition must not return ok.
             */
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

    const cJSON *id_item =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "id"
        );

    const cJSON *type_item =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "type"
        );

    const cJSON *command_item =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "command"
        );

    const cJSON *value_item =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "value"
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
        type_item->valuestring == NULL
    ) {
        ESP_LOGW(
            TAG,
            "JSON packet is missing string field: type"
        );

        cJSON_Delete(
            root
        );

        return;
    }

    if (
        strcasecmp(
            type_item->valuestring,
            "command"
        ) != 0
    ) {
        ESP_LOGW(
            TAG,
            "Unsupported JSON packet type"
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
        command_item->valuestring == NULL ||
        command_item->valuestring[0] == '\0'
    ) {
        ESP_LOGW(
            TAG,
            "JSON packet is missing string field: command"
        );

        cJSON_Delete(
            root
        );

        return;
    }

    const char *command =
        command_item->valuestring;

    if (
        value_item != NULL &&
        !cJSON_IsString(
            value_item
        )
    ) {
        ESP_LOGW(
            TAG,
            "JSON value field must be a string"
        );

        send_response(
            packet_id,
            "error",
            command,
            "invalid_value_type"
        );

        cJSON_Delete(
            root
        );

        return;
    }

    const char *value =
        NULL;

    if (
        cJSON_IsString(
            value_item
        ) &&
        value_item->valuestring != NULL
    ) {
        value =
            value_item->valuestring;
    }

    ESP_LOGI(
        TAG,
        "Validated command envelope: id=%d command=%s",
        packet_id,
        command
    );

    if (
        strcasecmp(
            command,
            "config.module"
        ) == 0
    ) {
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
            "Vehicle command rejected before Single-CAN configuration"
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

void singlecan_cmd_lock_doors(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: lock doors; mapping not implemented"
    );
}

void singlecan_cmd_unlock_doors(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: unlock doors; mapping not implemented"
    );
}

void singlecan_cmd_windows_down(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: windows down; mapping not implemented"
    );
}

void singlecan_cmd_windows_up(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: windows up; mapping not implemented"
    );
}

void singlecan_cmd_sunroof_open(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: sunroof open; mapping not implemented"
    );
}

void singlecan_cmd_sunroof_close(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: sunroof close; mapping not implemented"
    );
}

void singlecan_cmd_sunroof_vent(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: sunroof vent; mapping not implemented"
    );
}

void singlecan_cmd_headlights_on(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: headlights on; mapping not implemented"
    );
}

void singlecan_cmd_headlights_off(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: headlights off; mapping not implemented"
    );
}

void singlecan_cmd_fog_lights_on(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: fog lights on; mapping not implemented"
    );
}

void singlecan_cmd_fog_lights_off(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: fog lights off; mapping not implemented"
    );
}

void singlecan_cmd_interior_lights_on(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: interior lights on; mapping not implemented"
    );
}

void singlecan_cmd_interior_lights_off(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: interior lights off; mapping not implemented"
    );
}

void singlecan_cmd_ac_on(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: AC on; mapping not implemented"
    );
}

void singlecan_cmd_ac_off(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: AC off; mapping not implemented"
    );
}

void singlecan_cmd_fan_up(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: fan up; mapping not implemented"
    );
}

void singlecan_cmd_fan_down(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: fan down; mapping not implemented"
    );
}

void singlecan_cmd_audio_mute(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: audio mute; mapping not implemented"
    );
}

void singlecan_cmd_audio_unmute(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: audio unmute; mapping not implemented"
    );
}

void singlecan_cmd_trunk_open(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: trunk open; mapping not implemented"
    );
}

void singlecan_cmd_horn(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: horn; mapping not implemented"
    );
}

void singlecan_cmd_hazards_on(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: hazards on; mapping not implemented"
    );
}

void singlecan_cmd_hazards_off(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: hazards off; mapping not implemented"
    );
}

void singlecan_cmd_defrost_on(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: defrost on; mapping not implemented"
    );
}

void singlecan_cmd_defrost_off(void)
{
    ESP_LOGI(
        TAG,
        "CMD recognized: defrost off; mapping not implemented"
    );
}