#include "singlecan_commands.h"
#include "esp_log.h"
#include "singlecan_can.h"
#include <string.h>

static const char *TAG = "SingleCAN_CMDS";

// ------------------------------------------------------------
// INTERNAL DISPATCHER
// ------------------------------------------------------------
static void dispatch_command(const char *cmd)
{
    if (!cmd || strlen(cmd) == 0) {
        ESP_LOGW(TAG, "Empty command received");
        return;
    }

    ESP_LOGI(TAG, "Dispatching command: %s", cmd);

    // Doors
    if (strcmp(cmd, "lock_doors") == 0) {
        singlecan_cmd_lock_doors();
        return;
    }
    if (strcmp(cmd, "unlock_doors") == 0) {
        singlecan_cmd_unlock_doors();
        return;
    }

    // Windows
    if (strcmp(cmd, "windows_down") == 0) {
        singlecan_cmd_windows_down();
        return;
    }
    if (strcmp(cmd, "windows_up") == 0) {
        singlecan_cmd_windows_up();
        return;
    }

    // Sunroof
    if (strcmp(cmd, "sunroof_open") == 0) {
        singlecan_cmd_sunroof_open();
        return;
    }
    if (strcmp(cmd, "sunroof_close") == 0) {
        singlecan_cmd_sunroof_close();
        return;
    }
    if (strcmp(cmd, "sunroof_vent") == 0) {
        singlecan_cmd_sunroof_vent();
        return;
    }

    // Lights
    if (strcmp(cmd, "headlights_on") == 0) {
        singlecan_cmd_headlights_on();
        return;
    }
    if (strcmp(cmd, "headlights_off") == 0) {
        singlecan_cmd_headlights_off();
        return;
    }
    if (strcmp(cmd, "fog_lights_on") == 0) {
        singlecan_cmd_fog_lights_on();
        return;
    }
    if (strcmp(cmd, "fog_lights_off") == 0) {
        singlecan_cmd_fog_lights_off();
        return;
    }
    if (strcmp(cmd, "interior_lights_on") == 0) {
        singlecan_cmd_interior_lights_on();
        return;
    }
    if (strcmp(cmd, "interior_lights_off") == 0) {
        singlecan_cmd_interior_lights_off();
        return;
    }

    // Climate
    if (strcmp(cmd, "ac_on") == 0) {
        singlecan_cmd_ac_on();
        return;
    }
    if (strcmp(cmd, "ac_off") == 0) {
        singlecan_cmd_ac_off();
        return;
    }
    if (strcmp(cmd, "fan_up") == 0) {
        singlecan_cmd_fan_up();
        return;
    }
    if (strcmp(cmd, "fan_down") == 0) {
        singlecan_cmd_fan_down();
        return;
    }

    // Audio
    if (strcmp(cmd, "audio_mute") == 0) {
        singlecan_cmd_audio_mute();
        return;
    }
    if (strcmp(cmd, "audio_unmute") == 0) {
        singlecan_cmd_audio_unmute();
        return;
    }

    // Trunk
    if (strcmp(cmd, "trunk_open") == 0) {
        singlecan_cmd_trunk_open();
        return;
    }

    // Horn
    if (strcmp(cmd, "horn") == 0) {
        singlecan_cmd_horn();
        return;
    }

    // Hazards
    if (strcmp(cmd, "hazards_on") == 0) {
        singlecan_cmd_hazards_on();
        return;
    }
    if (strcmp(cmd, "hazards_off") == 0) {
        singlecan_cmd_hazards_off();
        return;
    }

    // Defrost
    if (strcmp(cmd, "defrost_on") == 0) {
        singlecan_cmd_defrost_on();
        return;
    }
    if (strcmp(cmd, "defrost_off") == 0) {
        singlecan_cmd_defrost_off();
        return;
    }

    // Unknown command
    ESP_LOGW(TAG, "Unknown command: %s", cmd);
}

// ------------------------------------------------------------
// PUBLIC ENTRY POINT — CALLED FROM BLUETOOTH SPP
// ------------------------------------------------------------
void singlecan_commands_process(const char *cmd)
{
    if (!cmd) {
        ESP_LOGE(TAG, "NULL command pointer");
        return;
    }

    // Trim whitespace
    char clean[64];
    memset(clean, 0, sizeof(clean));

    size_t len = strnlen(cmd, sizeof(clean) - 1);
    memcpy(clean, cmd, len);

    // Remove trailing newline if present
    if (clean[len - 1] == '\n' || clean[len - 1] == '\r') {
        clean[len - 1] = '\0';
    }

    dispatch_command(clean);
}

// ------------------------------------------------------------
// HIGH-LEVEL VEHICLE COMMANDS (SAFE STUBS)
// ------------------------------------------------------------
// These DO NOT send real CAN frames yet.
// They are the hook points where you will map
// actual vehicle CAN IDs once discovered via PCAN.
// ------------------------------------------------------------

// Doors
void singlecan_cmd_lock_doors(void)
{
    ESP_LOGI(TAG, "CMD: lock doors (TODO: map to CAN frame)");
}

void singlecan_cmd_unlock_doors(void)
{
    ESP_LOGI(TAG, "CMD: unlock doors (TODO: map to CAN frame)");
}

// Windows
void singlecan_cmd_windows_down(void)
{
    ESP_LOGI(TAG, "CMD: windows down (TODO: map to CAN frame)");
}

void singlecan_cmd_windows_up(void)
{
    ESP_LOGI(TAG, "CMD: windows up (TODO: map to CAN frame)");
}

// Sunroof
void singlecan_cmd_sunroof_open(void)
{
    ESP_LOGI(TAG, "CMD: sunroof open (TODO: map to CAN frame)");
}

void singlecan_cmd_sunroof_close(void)
{
    ESP_LOGI(TAG, "CMD: sunroof close (TODO: map to CAN frame)");
}

void singlecan_cmd_sunroof_vent(void)
{
    ESP_LOGI(TAG, "CMD: sunroof vent (TODO: map to CAN frame)");
}

// Lights
void singlecan_cmd_headlights_on(void)
{
    ESP_LOGI(TAG, "CMD: headlights on (TODO: map to CAN frame)");
}

void singlecan_cmd_headlights_off(void)
{
    ESP_LOGI(TAG, "CMD: headlights off (TODO: map to CAN frame)");
}

void singlecan_cmd_fog_lights_on(void)
{
    ESP_LOGI(TAG, "CMD: fog lights on (TODO: map to CAN frame)");
}

void singlecan_cmd_fog_lights_off(void)
{
    ESP_LOGI(TAG, "CMD: fog lights off (TODO: map to CAN frame)");
}

void singlecan_cmd_interior_lights_on(void)
{
    ESP_LOGI(TAG, "CMD: interior lights on (TODO: map to CAN frame)");
}

void singlecan_cmd_interior_lights_off(void)
{
    ESP_LOGI(TAG, "CMD: interior lights off (TODO: map to CAN frame)");
}

// Climate
void singlecan_cmd_ac_on(void)
{
    ESP_LOGI(TAG, "CMD: AC on (TODO: map to CAN frame)");
}

void singlecan_cmd_ac_off(void)
{
    ESP_LOGI(TAG, "CMD: AC off (TODO: map to CAN frame)");
}

void singlecan_cmd_fan_up(void)
{
    ESP_LOGI(TAG, "CMD: fan up (TODO: map to CAN frame)");
}

void singlecan_cmd_fan_down(void)
{
    ESP_LOGI(TAG, "CMD: fan down (TODO: map to CAN frame)");
}

// Audio
void singlecan_cmd_audio_mute(void)
{
    ESP_LOGI(TAG, "CMD: audio mute (TODO: map to CAN frame)");
}

void singlecan_cmd_audio_unmute(void)
{
    ESP_LOGI(TAG, "CMD: audio unmute (TODO: map to CAN frame)");
}

// Trunk
void singlecan_cmd_trunk_open(void)
{
    ESP_LOGI(TAG, "CMD: trunk open (TODO: map to CAN frame)");
}

// Horn
void singlecan_cmd_horn(void)
{
    ESP_LOGI(TAG, "CMD: horn (TODO: map to CAN frame)");
}

// Hazards
void singlecan_cmd_hazards_on(void)
{
    ESP_LOGI(TAG, "CMD: hazards on (TODO: map to CAN frame)");
}

void singlecan_cmd_hazards_off(void)
{
    ESP_LOGI(TAG, "CMD: hazards off (TODO: map to CAN frame)");
}

// Defrost
void singlecan_cmd_defrost_on(void)
{
    ESP_LOGI(TAG, "CMD: defrost on (TODO: map to CAN frame)");
}

void singlecan_cmd_defrost_off(void)
{
    ESP_LOGI(TAG, "CMD: defrost off (TODO: map to CAN frame)");
}
