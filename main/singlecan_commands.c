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
    MODULE_MODE_NOT_CONFIGURED = 0,
    MODULE_MODE_SINGLE_CAN,
    MODULE_MODE_DUAL_CAN
} module_mode_t;

typedef enum {
    COMMAND_RESULT_NOT_IMPLEMENTED = 0,
    COMMAND_RESULT_UNSUPPORTED
} command_result_t;

static module_mode_t s_module_mode =
    MODULE_MODE_NOT_CONFIGURED;

// ------------------------------------------------------------
// INTERNAL: SEND JSON RESPONSE TO BABYNODE AUTOMOTIVE
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

    bool response_valid =
        true;

    if (
        cJSON_AddNumberToObject(
            response,
            "id",
            packet_id
        ) == NULL
    ) {
        response_valid =
            false;
    }

    if (
        response_valid &&
        cJSON_AddStringToObject(
            response,
            "type",
            "response"
        ) == NULL
    ) {
        response_valid =
            false;
    }

    if (
        response_valid &&
        cJSON_AddStringToObject(
            response,
            "status",
            status
        ) == NULL
    ) {
        response_valid =
            false;
    }

    if (
        response_valid &&
        cJSON_AddStringToObject(
            response,
            "command",
            command
        ) == NULL
    ) {
        response_valid =
            false;
    }

    if (
        response_valid &&
        reason != NULL &&
        reason[0] != '\0' &&
        cJSON_AddStringToObject(
            response,
            "reason",
            reason
        ) == NULL
    ) {
        response_valid =
            false;
    }

    if (!response_valid) {
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

    const esp_err_t result =
        bt_spp_send(
            response_text
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to send response: %s",
            esp_err_to_name(
                result
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
// INTERNAL: STRICT REQUEST-ID VALIDATION
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
// INTERNAL: MODULE CONFIGURATION
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

        return false;
    }

    if (
        strcasecmp(
            value,
            "single"
        ) == 0
    ) {
        s_module_mode =
            MODULE_MODE_SINGLE_CAN;

        ESP_LOGI(
            TAG,
            "Module configured: SINGLE_CAN"
        );

        return true;
    }

    if (
        strcasecmp(
            value,
            "dual"
        ) == 0
    ) {
        s_module_mode =
            MODULE_MODE_DUAL_CAN;

        ESP_LOGI(
            TAG,
            "Module configured: DUAL_CAN"
        );

        return true;
    }

    ESP_LOGW(
        TAG,
        "Unknown module configuration"
    );

    return false;
}

// ------------------------------------------------------------
// INTERNAL: CANONICAL COMMAND DISPATCHER
// ------------------------------------------------------------

static command_result_t dispatch_command(
    const char *cmd
)
{
    if (
        cmd == NULL ||
        cmd[0] == '\0'
    ) {
        return COMMAND_RESULT_UNSUPPORTED;
    }

    ESP_LOGI(
        TAG,
        "Dispatching canonical command: %s",
        cmd
    );

    // Doors
    if (
        strcasecmp(
            cmd,
            "LOCK_DOORS"
        ) == 0 ||
        strcasecmp(
            cmd,
            "locks.all.lock"
        ) == 0
    ) {
        singlecan_cmd_lock_doors();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    if (
        strcasecmp(
            cmd,
            "UNLOCK_DOORS"
        ) == 0 ||
        strcasecmp(
            cmd,
            "locks.all.unlock"
        ) == 0
    ) {
        singlecan_cmd_unlock_doors();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    // Windows
    if (
        strcasecmp(
            cmd,
            "WINDOW_DRIVER_DOWN"
        ) == 0 ||
        strcasecmp(
            cmd,
            "WINDOW_PASSENGER_DOWN"
        ) == 0 ||
        strcasecmp(
            cmd,
            "WINDOWS_DOWN"
        ) == 0
    ) {
        singlecan_cmd_windows_down();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    if (
        strcasecmp(
            cmd,
            "WINDOW_DRIVER_UP"
        ) == 0 ||
        strcasecmp(
            cmd,
            "WINDOW_PASSENGER_UP"
        ) == 0 ||
        strcasecmp(
            cmd,
            "WINDOWS_UP"
        ) == 0
    ) {
        singlecan_cmd_windows_up();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    // Sunroof
    if (
        strcasecmp(
            cmd,
            "SUNROOF_OPEN"
        ) == 0 ||
        strcasecmp(
            cmd,
            "MOONROOF_OPEN"
        ) == 0
    ) {
        singlecan_cmd_sunroof_open();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    if (
        strcasecmp(
            cmd,
            "SUNROOF_CLOSE"
        ) == 0 ||
        strcasecmp(
            cmd,
            "MOONROOF_CLOSE"
        ) == 0
    ) {
        singlecan_cmd_sunroof_close();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    if (
        strcasecmp(
            cmd,
            "SUNROOF_VENT"
        ) == 0
    ) {
        singlecan_cmd_sunroof_vent();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    // Lighting
    if (
        strcasecmp(
            cmd,
            "HEADLIGHTS_ON"
        ) == 0
    ) {
        singlecan_cmd_headlights_on();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    if (
        strcasecmp(
            cmd,
            "HEADLIGHTS_OFF"
        ) == 0
    ) {
        singlecan_cmd_headlights_off();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    if (
        strcasecmp(
            cmd,
            "FOG_LIGHTS_ON"
        ) == 0
    ) {
        singlecan_cmd_fog_lights_on();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    if (
        strcasecmp(
            cmd,
            "FOG_LIGHTS_OFF"
        ) == 0
    ) {
        singlecan_cmd_fog_lights_off();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    if (
        strcasecmp(
            cmd,
            "INTERIOR_LIGHTS_ON"
        ) == 0
    ) {
        singlecan_cmd_interior_lights_on();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    if (
        strcasecmp(
            cmd,
            "INTERIOR_LIGHTS_OFF"
        ) == 0
    ) {
        singlecan_cmd_interior_lights_off();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    // Climate
    if (
        strcasecmp(
            cmd,
            "AC_ON"
        ) == 0
    ) {
        singlecan_cmd_ac_on();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    if (
        strcasecmp(
            cmd,
            "AC_OFF"
        ) == 0
    ) {
        singlecan_cmd_ac_off();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    if (
        strcasecmp(
            cmd,
            "FAN_UP"
        ) == 0
    ) {
        singlecan_cmd_fan_up();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    if (
        strcasecmp(
            cmd,
            "FAN_DOWN"
        ) == 0
    ) {
        singlecan_cmd_fan_down();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    // Audio
    if (
        strcasecmp(
            cmd,
            "AUDIO_MUTE"
        ) == 0
    ) {
        singlecan_cmd_audio_mute();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    if (
        strcasecmp(
            cmd,
            "AUDIO_UNMUTE"
        ) == 0
    ) {
        singlecan_cmd_audio_unmute();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    // Trunk
    if (
        strcasecmp(
            cmd,
            "TRUNK_OPEN"
        ) == 0
    ) {
        singlecan_cmd_trunk_open();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    // Horn
    if (
        strcasecmp(
            cmd,
            "HORN"
        ) == 0 ||
        strcasecmp(
            cmd,
            "HORN_SHORT"
        ) == 0
    ) {
        singlecan_cmd_horn();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    // Hazards
    if (
        strcasecmp(
            cmd,
            "HAZARDS_ON"
        ) == 0
    ) {
        singlecan_cmd_hazards_on();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    if (
        strcasecmp(
            cmd,
            "HAZARDS_OFF"
        ) == 0
    ) {
        singlecan_cmd_hazards_off();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    // Defrost
    if (
        strcasecmp(
            cmd,
            "DEFROST_ON"
        ) == 0 ||
        strcasecmp(
            cmd,
            "DEFROST_FRONT"
        ) == 0 ||
        strcasecmp(
            cmd,
            "DEFROST_REAR"
        ) == 0
    ) {
        singlecan_cmd_defrost_on();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    if (
        strcasecmp(
            cmd,
            "DEFROST_OFF"
        ) == 0 ||
        strcasecmp(
            cmd,
            "DEFROST_REAR_OFF"
        ) == 0
    ) {
        singlecan_cmd_defrost_off();
        return COMMAND_RESULT_NOT_IMPLEMENTED;
    }

    ESP_LOGW(
        TAG,
        "Unsupported canonical command"
    );

    return COMMAND_RESULT_UNSUPPORTED;
}

// ------------------------------------------------------------
// INTERNAL: PROCESS JSON COMMAND ENVELOPE
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

        /*
         * A command field may not be available, so this malformed
         * envelope is logged and rejected without fabricating one.
         */
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
            command_item->valuestring,
            "invalid_value_type"
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
            send_response(
                packet_id,
                "error",
                command,
                "invalid_module"
            );
        }

        cJSON_Delete(
            root
        );

        return;
    }

    if (
        s_module_mode ==
        MODULE_MODE_NOT_CONFIGURED
    ) {
        ESP_LOGW(
            TAG,
            "Vehicle command rejected before module configuration"
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
// PUBLIC ENTRY POINT: CALLED WITH ONE COMPLETE SPP FRAME
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

    /*
     * The legacy raw-command fallback has been removed.
     * Every request must use the documented JSON envelope.
     */
    process_json_packet(
        packet
    );
}

// ------------------------------------------------------------
// HIGH-LEVEL VEHICLE COMMANDS: SAFE STUBS
// ------------------------------------------------------------
// These functions do not send real CAN frames.
// Recognized stubs return unsupported/not_implemented.
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