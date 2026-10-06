#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------
// Public command processor (called from Bluetooth SPP)
// ------------------------------------------------------------
void singlecan_commands_process(const char *cmd);

// ------------------------------------------------------------
// High-level vehicle command API (safe stubs)
// ------------------------------------------------------------

// Doors
void singlecan_cmd_lock_doors(void);
void singlecan_cmd_unlock_doors(void);

// Windows
void singlecan_cmd_windows_down(void);
void singlecan_cmd_windows_up(void);

// Sunroof
void singlecan_cmd_sunroof_open(void);
void singlecan_cmd_sunroof_close(void);
void singlecan_cmd_sunroof_vent(void);

// Lights
void singlecan_cmd_headlights_on(void);
void singlecan_cmd_headlights_off(void);

void singlecan_cmd_fog_lights_on(void);
void singlecan_cmd_fog_lights_off(void);

void singlecan_cmd_interior_lights_on(void);
void singlecan_cmd_interior_lights_off(void);

// Climate
void singlecan_cmd_ac_on(void);
void singlecan_cmd_ac_off(void);

void singlecan_cmd_fan_up(void);
void singlecan_cmd_fan_down(void);

// Audio
void singlecan_cmd_audio_mute(void);
void singlecan_cmd_audio_unmute(void);

// Trunk
void singlecan_cmd_trunk_open(void);

// Horn
void singlecan_cmd_horn(void);

// Hazards
void singlecan_cmd_hazards_on(void);
void singlecan_cmd_hazards_off(void);

// Defrost
void singlecan_cmd_defrost_on(void);
void singlecan_cmd_defrost_off(void);

#ifdef __cplusplus
}
#endif
