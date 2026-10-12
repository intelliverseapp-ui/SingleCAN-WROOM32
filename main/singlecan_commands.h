#pragma once

#include <stdint.h>

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
 *
 * session_id is the SPP session that delivered the frame. Every
 * command except protocol.handshake must carry an equal, nonzero
 * "session_id" field, and that session must still be active.
 * Otherwise the packet is dropped before any side effect.
 */
void singlecan_commands_process(
    const char *packet,
    uint32_t session_id
);

#ifdef __cplusplus
}
#endif
