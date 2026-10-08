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
 * the SPP callback, and the BabyNodeCAN SPP server.
 *
 * The initialization request is asynchronous. Successful return
 * means initialization was accepted, not necessarily that the
 * SPP server has completed startup.
 */
esp_err_t bt_spp_init(void);

// ------------------------------------------------------------
// SPP CONNECTION STATE
// ------------------------------------------------------------

/**
 * Returns nonzero when an authenticated SPP client is connected.
 *
 * This function reports the current connection state managed by
 * bt_spp.c.
 */
int bt_spp_is_connected(void);

/**
 * Returns the active SPP connection handle.
 *
 * Returns zero when no SPP client is connected.
 *
 * External modules should not call esp_spp_write() directly with
 * this handle. All outbound messages must use bt_spp_send().
 */
uint32_t bt_spp_get_handle(void);

// ------------------------------------------------------------
// SERIALIZED SPP OUTPUT
// ------------------------------------------------------------

/**
 * Queues one newline-delimited text message for transmission to
 * the active SPP client.
 *
 * The supplied message must:
 * - Be null-terminated
 * - Be nonempty
 * - Not already include the framing newline
 * - Fit within the configured outbound-message limit
 *
 * bt_spp.c owns:
 * - The active SPP handle
 * - Outbound-message serialization
 * - Newline framing
 * - Congestion handling
 * - ESP_SPP_WRITE_EVT completion handling
 * - Disconnect cleanup
 *
 * No other source file may call esp_spp_write() directly.
 *
 * Returns:
 * - ESP_OK when the message is accepted for queued transmission
 * - ESP_ERR_INVALID_ARG for a null or empty message
 * - ESP_ERR_INVALID_SIZE when the message is too large
 * - ESP_ERR_INVALID_STATE when no client is connected or the
 *   SPP writer is unavailable
 * - ESP_ERR_NO_MEM when the outbound queue is full
 * - Another ESP-IDF error if the request cannot be accepted
 *
 * ESP_OK means the message entered the SPP writer pipeline.
 * It does not mean that ESP_SPP_WRITE_EVT has confirmed delivery.
 */
esp_err_t bt_spp_send(
    const char *message
);

#ifdef __cplusplus
}
#endif