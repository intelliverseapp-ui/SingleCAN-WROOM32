#pragma once

#include "esp_err.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------
// BLUETOOTH CLASSIC SPP INITIALIZATION
// ------------------------------------------------------------

/**
 * Initializes the Bluetooth Classic controller, Bluedroid,
 * the SPP callback, the completion-driven outbound writer, and the
 * BabyNodeCAN SPP server.
 *
 * Initialization of the SPP server is asynchronous.
 *
 * ESP_OK means the initialization request was accepted. The caller
 * must use bt_spp_wait_until_ready() before reporting the Bluetooth
 * command backend as ready.
 */
esp_err_t bt_spp_init(void);

// ------------------------------------------------------------
// SPP SERVER READINESS
// ------------------------------------------------------------

/**
 * Returns nonzero only after ESP_SPP_START_EVT confirms that the SPP
 * server started successfully.
 *
 * A connected client is not required for the server to be ready.
 */
int bt_spp_is_server_ready(void);

/**
 * Waits for the asynchronous SPP server startup result.
 *
 * timeout_ms specifies the maximum number of milliseconds to wait.
 *
 * Returns:
 * - ESP_OK when ESP_SPP_START_EVT confirms successful server startup
 * - ESP_ERR_TIMEOUT when startup is not confirmed before timeout_ms
 * - ESP_ERR_INVALID_STATE when the readiness mechanism has not been
 *   initialized or startup fails without a specific error
 * - Another ESP-IDF error when startup failure information is
 *   available
 */
esp_err_t bt_spp_wait_until_ready(
    uint32_t timeout_ms
);

// ------------------------------------------------------------
// ACTIVE SPP CONNECTION
// ------------------------------------------------------------

/**
 * Returns nonzero when the one permitted SPP client is connected.
 *
 * SingleCAN supports exactly one active SPP client. Additional
 * connection attempts are rejected by bt_spp.c and cannot replace
 * the active session.
 *
 * This is separate from server readiness. The server may be ready
 * while no client is connected.
 */
int bt_spp_is_connected(void);

/**
 * Returns the active SPP connection handle.
 *
 * Returns zero when no client owns the SPP session.
 *
 * External modules must not call esp_spp_write() or
 * esp_spp_disconnect() with this handle. Connection ownership,
 * disconnection, and all outbound writes belong to bt_spp.c.
 */
uint32_t bt_spp_get_handle(void);

// ------------------------------------------------------------
// SERIALIZED SPP OUTPUT
// ------------------------------------------------------------

/**
 * Queues one newline-delimited response for transmission to the
 * active SPP client.
 *
 * The supplied message must:
 * - Be null-terminated
 * - Be nonempty
 * - Not include the framing newline
 * - Fit within the configured outbound-message limit
 *
 * bt_spp.c owns:
 * - Single-client connection ownership
 * - Active handle and session generation
 * - In-flight write ownership
 * - Session-transition cancellation
 * - Outbound-message serialization
 * - Newline framing
 * - Congestion handling
 * - Bounded write-completion waiting
 * - ESP_SPP_WRITE_EVT completion handling
 * - Stale-event rejection
 * - Disconnect cleanup
 *
 * No other source file may call esp_spp_write() directly.
 *
 * Returns:
 * - ESP_OK when the message enters the writer queue
 * - ESP_ERR_INVALID_ARG for a null or empty message
 * - ESP_ERR_INVALID_SIZE when the message is too large
 * - ESP_ERR_INVALID_STATE when the server, connection, session, or
 *   writer is unavailable
 * - ESP_ERR_NO_MEM when the outbound queue cannot accept the message
 * - Another ESP-IDF error when the request cannot be accepted
 *
 * ESP_OK means the message entered the writer pipeline. It does not
 * mean ESP_SPP_WRITE_EVT confirmed successful transmission.
 */
esp_err_t bt_spp_send(
    const char *message
);

#ifdef __cplusplus
}
#endif