#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------
// COMMAND SESSION STATE
// ------------------------------------------------------------

/**
 * Resets the command processor for a new or closed Bluetooth
 * session.
 *
 * After reset, vehicle commands remain blocked until the Android
 * frontend sends a valid:
 *
 * {"type":"command","command":"config.module","value":"single"}
 */
void singlecan_commands_reset_session(void);

/**
 * Returns nonzero when the current Bluetooth session has completed
 * valid Single-CAN module configuration.
 */
int singlecan_commands_is_configured(void);

// ------------------------------------------------------------
// PUBLIC COMMAND PROCESSOR
// ------------------------------------------------------------

/**
 * Processes one complete, newline-delimited JSON command frame
 * received from Bluetooth SPP.
 *
 * The caller must provide a null-terminated string containing
 * exactly one complete JSON object without the framing newline.
 */
void singlecan_commands_process(
    const char *packet
);

// ------------------------------------------------------------
// HIGH-LEVEL VEHICLE COMMAND API
// SAFE STUBS UNTIL VERIFIED HONDA CAN MAPPINGS EXIST
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

// Lighting
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