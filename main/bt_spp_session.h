#pragma once

#include "bt_spp_writer.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initializes the single-client SPP session owner.
 *
 * The initial state has no connected client and session generation
 * zero.
 */
void bt_spp_session_init(void);

/**
 * Attempts to accept one SPP client connection.
 *
 * Exactly one client may own the active session. An additional
 * connection cannot replace the current owner.
 *
 * Returns nonzero when the handle becomes the active connection.
 * Returns zero when the connection must be rejected.
 */
int bt_spp_session_accept(
    uint32_t handle,
    uint32_t *session_id
);

/**
 * Returns nonzero when the supplied handle owns the active session.
 */
int bt_spp_session_handle_is_active(
    uint32_t handle
);

/**
 * Returns nonzero when the supplied handle and session generation
 * identify the active session.
 */
int bt_spp_session_matches(
    uint32_t handle,
    uint32_t session_id
);

/**
 * Updates the congestion state of the active connection.
 *
 * Returns nonzero when the event handle matches the active session.
 * Returns zero for stale or unrelated handles.
 */
int bt_spp_session_set_congested(
    uint32_t handle,
    int congested
);

/**
 * Clears the active client only when handle still owns the session.
 *
 * The session generation is advanced so queued or in-flight work
 * from the closed connection becomes stale.
 *
 * Returns nonzero when the active session was closed.
 * Returns zero for a stale or unrelated close event.
 */
int bt_spp_session_close(
    uint32_t handle
);

/**
 * Forcibly clears the active session and advances its generation.
 *
 * Use only after a verified active-session failure when the Bluetooth
 * stack cannot complete the requested disconnect.
 */
int bt_spp_session_force_close_if_matches(
    uint32_t handle,
    uint32_t session_id
);

void bt_spp_session_force_close(void);

/**
 * Copies a synchronized snapshot into the writer session structure.
 */
void bt_spp_session_get_writer_snapshot(
    bt_spp_writer_session_t *session
);

/**
 * Returns nonzero when one client currently owns the SPP session.
 */
int bt_spp_session_is_connected(void);

/**
 * Returns the active SPP connection handle.
 *
 * Returns zero when no client owns the session.
 */
uint32_t bt_spp_session_get_handle(void);

/**
 * Returns the current session generation.
 */
uint32_t bt_spp_session_get_id(void);

#ifdef __cplusplus
}
#endif