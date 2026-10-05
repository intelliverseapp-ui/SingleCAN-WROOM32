#include "singlecan_commands.h"
#include "esp_log.h"
#include "singlecan_can.h"

static const char *TAG = "SingleCAN_CMDS";

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
