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

#ifdef __cplusplus
}
#endif
