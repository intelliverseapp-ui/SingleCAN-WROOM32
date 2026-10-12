#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Receives one completed, null-terminated SPP text frame.
 *
 * The supplied pointer remains valid only for the duration of the
 * callback. The receiver must process or copy the frame immediately.
 * session_id identifies the SPP session that delivered the final
 * fragment of the frame.
 */
typedef void (*bt_spp_frame_handler_t)(
    const char *frame,
    uint32_t session_id
);

/**
 * Initializes the bounded SPP receive framer.
 *
 * The handler is called only after one complete valid LF or CRLF
 * terminated frame has been assembled.
 *
 * Returns nonzero on success and zero when handler is invalid.
 */
int bt_spp_framer_init(
    bt_spp_frame_handler_t handler
);

/**
 * Clears all partial-frame and discard state.
 *
 * Call this whenever an SPP session opens or closes so incomplete
 * data cannot cross connection boundaries.
 */
void bt_spp_framer_reset(void);

/**
 * Processes one fragment of bytes received through SPP.
 *
 * The framer:
 * - Supports fragmented and coalesced messages
 * - Accepts LF or CRLF termination
 * - Rejects carriage returns outside a CRLF terminator
 * - Rejects binary and disallowed control bytes
 * - Enforces the maximum frame size
 * - Discards an invalid frame through its next newline
 */
void bt_spp_framer_process(
    const uint8_t *data,
    size_t data_length,
    uint32_t session_id
);

#ifdef __cplusplus
}
#endif