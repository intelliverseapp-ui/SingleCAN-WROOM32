#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Constructs and queues one JSON protocol response.
 *
 * The response contains:
 * - id
 * - type=response
 * - status
 * - command
 * - optional reason
 *
 * Returns:
 * - ESP_OK when the serialized response is accepted by the SPP writer
 * - ESP_ERR_INVALID_ARG when required fields are invalid
 * - ESP_ERR_NO_MEM when JSON construction or serialization fails
 * - another ESP-IDF error returned by the Bluetooth response path
 */
esp_err_t singlecan_response_send(
    int packet_id,
    const char *status,
    const char *command,
    const char *reason
);

#ifdef __cplusplus
}
#endif
