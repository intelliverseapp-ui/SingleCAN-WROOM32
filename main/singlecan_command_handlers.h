#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------
// NON-TRANSMITTING VEHICLE COMMAND HANDLERS
// ------------------------------------------------------------

/*
 * These handlers recognize allowlisted vehicle commands but do not
 * transmit CAN frames.
 *
 * Each handler remains a safe stub until a corresponding Honda CAN
 * mapping has been captured, independently verified, reviewed, and
 * added through the private verified-mapping transmission gate.
 */

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

// Exterior lighting

void singlecan_cmd_headlights_on(void);

void singlecan_cmd_headlights_off(void);

void singlecan_cmd_fog_lights_on(void);

void singlecan_cmd_fog_lights_off(void);

// Interior lighting

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
