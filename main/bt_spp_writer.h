#pragma once

#include "esp_err.h"
#include "esp_spp_api.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Snapshot of the active SPP session required by the writer.
 *
 * bt_spp.c remains the owner of Bluetooth connection state.
 * bt_spp_writer.c receives only synchronized snapshots.
 */
typedef struct {
    uint32_t handle;
    uint32_t session_id;

    int server_ready;
    int connected;
    int congested;
} bt_spp_writer_session_t;

/**
 * Cumulative response-delivery failure counters.
 *
 * Counters remain available across Bluetooth sessions and never
 * contain response content or other sensitive data.
 */
typedef struct {
    uint32_t queue_full;
    uint32_t immediate_write_failures;
    uint32_t asynchronous_write_failures;
    uint32_t write_timeouts;
} bt_spp_writer_stats_t;

/**
 * Retrieves a synchronized snapshot of the active SPP session.
 */
typedef void (*bt_spp_writer_session_provider_t)(
    bt_spp_writer_session_t *session
);

/**
 * Requests disconnection of the specified failed or timed-out SPP
 * session.
 *
 * The callback must verify that the handle and session still identify
 * the active connection before disconnecting it.
 */
typedef void (*bt_spp_writer_disconnect_handler_t)(
    uint32_t handle,
    uint32_t session_id
);

/**
 * Initializes the completion-driven outbound writer.
 *
 * Returns:
 * - ESP_OK when the queue, event group, and writer task are ready
 * - ESP_ERR_INVALID_ARG when a required callback is missing
 * - ESP_ERR_INVALID_STATE when already initialized
 * - ESP_ERR_NO_MEM when a FreeRTOS object cannot be created
 */
esp_err_t bt_spp_writer_init(
    bt_spp_writer_session_provider_t session_provider,
    bt_spp_writer_disconnect_handler_t disconnect_handler
);

/**
 * Queues one response for the specified SPP session.
 *
 * The message is copied into writer-owned storage and newline framing
 * is added internally.
 *
 * Returns:
 * - ESP_OK when accepted by the bounded queue
 * - ESP_ERR_INVALID_ARG for a null or empty message
 * - ESP_ERR_INVALID_SIZE when the message is too large
 * - ESP_ERR_INVALID_STATE when the writer or session is unavailable
 * - ESP_ERR_NO_MEM when the bounded queue is full
 */
esp_err_t bt_spp_writer_send(
    const char *message,
    uint32_t session_id
);

/**
 * Informs the writer that an SPP connection became available.
 */
void bt_spp_writer_on_connected(void);

/**
 * Cancels pending and in-flight work associated with the previous
 * session and wakes any task waiting for completion.
 */
void bt_spp_writer_on_disconnected(void);

/**
 * Updates writer availability after an SPP congestion event.
 */
void bt_spp_writer_on_congestion_changed(void);

/**
 * Delivers one ESP_SPP_WRITE_EVT to the writer.
 *
 * Returns nonzero when the event matched the current in-flight write.
 * Returns zero for a stale or unrelated event.
 */
int bt_spp_writer_on_write_event(
    const esp_spp_cb_param_t *parameters
);

/**
 * Copies the cumulative response-delivery failure counters.
 */
void bt_spp_writer_get_stats(
    bt_spp_writer_stats_t *stats
);

#ifdef __cplusplus
}
#endif