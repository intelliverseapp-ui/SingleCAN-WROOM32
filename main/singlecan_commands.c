#include "singlecan_commands.h"

#include "bt_spp.h"
#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "singlecan_can.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "SingleCAN_CMDS";

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
    cJSON *response = cJSON_CreateObject();

    if (response == NULL) {
        ESP_LOGE(TAG, "Failed to create response JSON object");
        return;
    }

    if (
        cJSON_AddNumberToObject(
            response,
            "id",
            packet_id
        ) == NULL
    ) {
        ESP_LOGE(TAG, "Failed to add response id");
        cJSON_Delete(response);
        return;
    }

    if (
        cJSON_AddStringToObject(
            response,
            "type",
            "response"
        ) == NULL
    ) {
        ESP_LOGE(TAG, "Failed to add response type");
        cJSON_Delete(response);
        return;
    }

    if (
        cJSON_AddStringToObject(
            response,
            "status",
            status
        ) == NULL
    ) {
        ESP_LOGE(TAG, "Failed to add response status");
        cJSON_Delete(response);
        return;
    }

    if (
        command != NULL &&
        cJSON_AddStringToObject(
            response,
            "command",
            command
        ) == NULL
    ) {
        ESP_LOGE(TAG, "Failed to add response command");
        cJSON_Delete(response);
        return;
    }

    if (
        reason != NULL &&
        cJSON_AddStringToObject(
            response,
            "reason",
            reason
        ) == NULL
    ) {
        ESP_LOGE(TAG, "Failed to add response reason");
        cJSON_Delete(response);
        return;
    }

    char *response_text =
        cJSON_PrintUnformatted(response);

    if (response_text == NULL) {
        ESP_LOGE(TAG, "Failed to serialize response JSON");
        cJSON_Delete(response);
        return;
    }

    ESP_LOGI(
        TAG,
        "Sending response: %s",
        response_text
    );

    esp_err_t result =
        bt_spp_send(response_text);

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to send response: %s",
            esp_err_to_name(result)
        );
    }

    cJSON_free(response_text);
    cJSON_Delete(response);
}

// ------------------------------------------------------------
// INTERNAL: MODULE CONFIGURATION
// ------------------------------------------------------------
static bool process_module_config(const char *value)
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
        ESP_LOGI(
            TAG,
            "Module configured: DUAL_CAN"
        );

        return true;
    }

    ESP_LOGW(
        TAG,
        "Unknown module configuration: %s",
        value
    );

    return false;
}

// ------------------------------------------------------------
// INTERNAL: CANONICAL COMMAND DISPATCHER
// ------------------------------------------------------------
static bool dispatch_command(const char *cmd)
{
    if (
        cmd == NULL ||
        cmd[0] == '\0'
    ) {
        ESP_LOGW(
            TAG,
            "Empty command received"
        );

        return false;
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
        return true;
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
        return true;
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
        return true;
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
        return true;
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
        return true;
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
        return true;
    }

    if (
        strcasecmp(
            cmd,
            "SUNROOF_VENT"
        ) == 0
    ) {
        singlecan_cmd_sunroof_vent();
        return true;
    }

    // Lights
    if (
        strcasecmp(
            cmd,
            "HEADLIGHTS_ON"
        ) == 0
    ) {
        singlecan_cmd_headlights_on();
        return true;
    }

    if (
        strcasecmp(
            cmd,
            "HEADLIGHTS_OFF"
        ) == 0
    ) {
        singlecan_cmd_headlights_off();
        return true;
    }

    if (
        strcasecmp(
            cmd,
            "FOG_LIGHTS_ON"
        ) == 0
    ) {
        singlecan_cmd_fog_lights_on();
        return true;
    }

    if (
        strcasecmp(
            cmd,
            "FOG_LIGHTS_OFF"
        ) == 0
    ) {
        singlecan_cmd_fog_lights_off();
        return true;
    }

    if (
        strcasecmp(
            cmd,
            "INTERIOR_LIGHTS_ON"
        ) == 0
    ) {
        singlecan_cmd_interior_lights_on();
        return true;
    }

    if (
        strcasecmp(
            cmd,
            "INTERIOR_LIGHTS_OFF"
        ) == 0
    ) {
        singlecan_cmd_interior_lights_off();
        return true;
    }

    // Climate
    if (
        strcasecmp(
            cmd,
            "AC_ON"
        ) == 0
    ) {
        singlecan_cmd_ac_on();
        return true;
    }

    if (
        strcasecmp(
            cmd,
            "AC_OFF"
        ) == 0
    ) {
        singlecan_cmd_ac_off();
        return true;
    }

    if (
        strcasecmp(
            cmd,
            "FAN_UP"
        ) == 0
    ) {
        singlecan_cmd_fan_up();
        return true;
    }

    if (
        strcasecmp(
            cmd,
            "FAN_DOWN"
        ) == 0
    ) {
        singlecan_cmd_fan_down();
        return true;
    }

    // Audio
    if (
        strcasecmp(
            cmd,
            "AUDIO_MUTE"
        ) == 0
    ) {
        singlecan_cmd_audio_mute();
        return true;
    }

    if (
        strcasecmp(
            cmd,
            "AUDIO_UNMUTE"
        ) == 0
    ) {
        singlecan_cmd_audio_unmute();
        return true;
    }

    // Trunk
    if (
        strcasecmp(
            cmd,
            "TRUNK_OPEN"
        ) == 0
    ) {
        singlecan_cmd_trunk_open();
        return true;
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
        return true;
    }

    // Hazards
    if (
        strcasecmp(
            cmd,
            "HAZARDS_ON"
        ) == 0
    ) {
        singlecan_cmd_hazards_on();
        return true;
    }

    if (
        strcasecmp(
            cmd,
            "HAZARDS_OFF"
        ) == 0
    ) {
        singlecan_cmd_hazards_off();
        return true;
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
        return true;
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
        return true;
    }

    ESP_LOGW(
        TAG,
        "Unsupported canonical command: %s",
        cmd
    );

    return false;
}

// ------------------------------------------------------------
// INTERNAL: PROCESS JSON COMMAND ENVELOPE
// ------------------------------------------------------------
static bool process_json_packet(const char *packet)
{
    cJSON *root =
        cJSON_Parse(packet);

    if (root == NULL) {
        const char *error_pointer =
            cJSON_GetErrorPtr();

        if (error_pointer != NULL) {
            ESP_LOGW(
                TAG,
                "JSON parse failed near: %s",
                error_pointer
            );
        } else {
            ESP_LOGW(
                TAG,
                "JSON parse failed"
            );
        }

        return false;
    }

    if (!cJSON_IsObject(root)) {
        ESP_LOGW(
            TAG,
            "JSON root is not an object"
        );

        cJSON_Delete(root);
        return true;
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

    int packet_id = 0;

    if (cJSON_IsNumber(id_item)) {
        packet_id =
            id_item->valueint;

        ESP_LOGI(
            TAG,
            "Packet ID: %d",
            packet_id
        );
    } else {
        ESP_LOGW(
            TAG,
            "JSON packet is missing numeric field: id"
        );
    }

    if (
        !cJSON_IsString(type_item) ||
        type_item->valuestring == NULL
    ) {
        ESP_LOGW(
            TAG,
            "JSON packet is missing string field: type"
        );

        send_response(
            packet_id,
            "error",
            NULL,
            "missing_type"
        );

        cJSON_Delete(root);
        return true;
    }

    if (
        strcasecmp(
            type_item->valuestring,
            "command"
        ) != 0
    ) {
        ESP_LOGW(
            TAG,
            "Unsupported JSON packet type: %s",
            type_item->valuestring
        );

        send_response(
            packet_id,
            "error",
            NULL,
            "unsupported_type"
        );

        cJSON_Delete(root);
        return true;
    }

    if (
        !cJSON_IsString(command_item) ||
        command_item->valuestring == NULL ||
        command_item->valuestring[0] == '\0'
    ) {
        ESP_LOGW(
            TAG,
            "JSON packet is missing string field: command"
        );

        send_response(
            packet_id,
            "error",
            NULL,
            "missing_command"
        );

        cJSON_Delete(root);
        return true;
    }

    const char *command =
        command_item->valuestring;

    const char *value = NULL;

    if (
        cJSON_IsString(value_item) &&
        value_item->valuestring != NULL
    ) {
        value =
            value_item->valuestring;
    }

    ESP_LOGI(
        TAG,
        "Parsed command: %s",
        command
    );

    if (value != NULL) {
        ESP_LOGI(
            TAG,
            "Parsed value: %s",
            value
        );
    }

    bool command_succeeded = false;

    if (
        strcasecmp(
            command,
            "config.module"
        ) == 0
    ) {
        command_succeeded =
            process_module_config(value);
    } else {
        command_succeeded =
            dispatch_command(command);
    }

    if (command_succeeded) {
        send_response(
            packet_id,
            "ok",
            command,
            NULL
        );
    } else {
        send_response(
            packet_id,
            "unsupported",
            command,
            "unknown_command"
        );
    }

    cJSON_Delete(root);
    return true;
}

// ------------------------------------------------------------
// PUBLIC ENTRY POINT: CALLED FROM BLUETOOTH SPP
// ------------------------------------------------------------
void singlecan_commands_process(const char *packet)
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

    ESP_LOGI(
        TAG,
        "Received packet: %s",
        packet
    );

    if (process_json_packet(packet)) {
        return;
    }

    ESP_LOGW(
        TAG,
        "Packet is not valid JSON; trying legacy command format"
    );

    dispatch_command(packet);
}

// ------------------------------------------------------------
// HIGH-LEVEL VEHICLE COMMANDS: SAFE STUBS
// ------------------------------------------------------------
// These functions do not send real CAN frames yet.
// Vehicle-specific CAN mappings will be added after validation.
// ------------------------------------------------------------

// Doors
void singlecan_cmd_lock_doors(void)
{
    ESP_LOGI(
        TAG,
        "CMD: lock doors (TODO: map to CAN frame)"
    );
}

void singlecan_cmd_unlock_doors(void)
{
    ESP_LOGI(
        TAG,
        "CMD: unlock doors (TODO: map to CAN frame)"
    );
}

// Windows
void singlecan_cmd_windows_down(void)
{
    ESP_LOGI(
        TAG,
        "CMD: windows down (TODO: map to CAN frame)"
    );
}

void singlecan_cmd_windows_up(void)
{
    ESP_LOGI(
        TAG,
        "CMD: windows up (TODO: map to CAN frame)"
    );
}

// Sunroof
void singlecan_cmd_sunroof_open(void)
{
    ESP_LOGI(
        TAG,
        "CMD: sunroof open (TODO: map to CAN frame)"
    );
}

void singlecan_cmd_sunroof_close(void)
{
    ESP_LOGI(
        TAG,
        "CMD: sunroof close (TODO: map to CAN frame)"
    );
}

void singlecan_cmd_sunroof_vent(void)
{
    ESP_LOGI(
        TAG,
        "CMD: sunroof vent (TODO: map to CAN frame)"
    );
}

// Lights
void singlecan_cmd_headlights_on(void)
{
    ESP_LOGI(
        TAG,
        "CMD: headlights on (TODO: map to CAN frame)"
    );
}

void singlecan_cmd_headlights_off(void)
{
    ESP_LOGI(
        TAG,
        "CMD: headlights off (TODO: map to CAN frame)"
    );
}

void singlecan_cmd_fog_lights_on(void)
{
    ESP_LOGI(
        TAG,
        "CMD: fog lights on (TODO: map to CAN frame)"
    );
}

void singlecan_cmd_fog_lights_off(void)
{
    ESP_LOGI(
        TAG,
        "CMD: fog lights off (TODO: map to CAN frame)"
    );
}

void singlecan_cmd_interior_lights_on(void)
{
    ESP_LOGI(
        TAG,
        "CMD: interior lights on (TODO: map to CAN frame)"
    );
}

void singlecan_cmd_interior_lights_off(void)
{
    ESP_LOGI(
        TAG,
        "CMD: interior lights off (TODO: map to CAN frame)"
    );
}

// Climate
void singlecan_cmd_ac_on(void)
{
    ESP_LOGI(
        TAG,
        "CMD: AC on (TODO: map to CAN frame)"
    );
}

void singlecan_cmd_ac_off(void)
{
    ESP_LOGI(
        TAG,
        "CMD: AC off (TODO: map to CAN frame)"
    );
}

void singlecan_cmd_fan_up(void)
{
    ESP_LOGI(
        TAG,
        "CMD: fan up (TODO: map to CAN frame)"
    );
}

void singlecan_cmd_fan_down(void)
{
    ESP_LOGI(
        TAG,
        "CMD: fan down (TODO: map to CAN frame)"
    );
}

// Audio
void singlecan_cmd_audio_mute(void)
{
    ESP_LOGI(
        TAG,
        "CMD: audio mute (TODO: map to CAN frame)"
    );
}

void singlecan_cmd_audio_unmute(void)
{
    ESP_LOGI(
        TAG,
        "CMD: audio unmute (TODO: map to CAN frame)"
    );
}

// Trunk
void singlecan_cmd_trunk_open(void)
{
    ESP_LOGI(
        TAG,
        "CMD: trunk open (TODO: map to CAN frame)"
    );
}

// Horn
void singlecan_cmd_horn(void)
{
    ESP_LOGI(
        TAG,
        "CMD: horn (TODO: map to CAN frame)"
    );
}

// Hazards
void singlecan_cmd_hazards_on(void)
{
    ESP_LOGI(
        TAG,
        "CMD: hazards on (TODO: map to CAN frame)"
    );
}

void singlecan_cmd_hazards_off(void)
{
    ESP_LOGI(
        TAG,
        "CMD: hazards off (TODO: map to CAN frame)"
    );
}

// Defrost
void singlecan_cmd_defrost_on(void)
{
    ESP_LOGI(
        TAG,
        "CMD: defrost on (TODO: map to CAN frame)"
    );
}

void singlecan_cmd_defrost_off(void)
{
    ESP_LOGI(
        TAG,
        "CMD: defrost off (TODO: map to CAN frame)"
    );
}