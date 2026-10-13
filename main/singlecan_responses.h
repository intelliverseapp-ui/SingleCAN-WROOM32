#pragma once

#include "esp_err.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Constructs and queues one JSON protocol response.
 *
 * The response contains:
 * - id
 * - session_id (the originating nonzero SPP session; never looked up
 *   again at response time)
 * - version=1
 * - type=response
 * - status
 * - command
 * - optional reason
 *
 * Returns:
 * - ESP_OK when the serialized response is accepted by the SPP writer
 * - ESP_ERR_INVALID_ARG when required fields or session_id are invalid
 * - ESP_ERR_NO_MEM when JSON construction or serialization fails
 * - another ESP-IDF error returned by the Bluetooth response path
 */
esp_err_t singlecan_response_send(
    int packet_id,
    uint32_t session_id,
    const char *status,
    const char *command,
    const char *reason
);

/**
 * Sends the protocol handshake response for the active SPP session.
 *
 * The response truthfully reports:
 * - protocol version 1
 * - the active nonzero SPP session generation
 * - Single-CAN module support
 * - CAN transmission disabled
 * - vehicle commands available as non-transmitting stubs only
 */
esp_err_t singlecan_handshake_response_send(
    int packet_id,
    uint32_t session_id
);

#ifdef __cplusplus
}
#endif
