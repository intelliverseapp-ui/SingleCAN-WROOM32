#pragma once

#include "driver/twai.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------
// ESP32-WROOM32 DEVKIT V1 GPIO ASSIGNMENTS
// ------------------------------------------------------------

#define SINGLECAN_TX_PIN GPIO_NUM_5
#define SINGLECAN_RX_PIN GPIO_NUM_4

// ------------------------------------------------------------
// TWAI BUS TIMING
// ------------------------------------------------------------

#define SINGLECAN_BITRATE \
    TWAI_TIMING_CONFIG_500KBITS()

// ------------------------------------------------------------
// VERIFIED VEHICLE COMMAND MAPPINGS
// ------------------------------------------------------------

/**
 * Identifies a reviewed vehicle-specific CAN mapping.
 *
 * No Honda Accord mappings have been verified yet, so the only
 * available value is NONE.
 *
 * New mapping identifiers must be added individually after:
 *
 * - Capture through PCAN hardware and PCAN-Explorer 7
 * - Repeated observation and comparison
 * - Independent identifier, DLC, and payload verification
 * - Phase 1 safety-scope review
 * - Explicit implementation and test coverage
 *
 * Arbitrary CAN identifiers and payloads must never come from the
 * Bluetooth client or another generic runtime caller.
 */
typedef enum {
    SINGLECAN_VERIFIED_COMMAND_NONE = 0
} singlecan_verified_command_t;

// ------------------------------------------------------------
// TWAI INITIALIZATION AND SHUTDOWN
// ------------------------------------------------------------

/**
 * Installs and starts the ESP32 TWAI driver in normal mode.
 *
 * Normal mode is retained because SingleCAN will eventually transmit
 * verified vehicle-control frames. Current command handlers remain
 * non-transmitting stubs.
 */
esp_err_t singlecan_init(void);

/**
 * Stops and uninstalls the TWAI driver.
 */
esp_err_t singlecan_stop(void);

// ------------------------------------------------------------
// TWAI ENABLE AND DISABLE STATE
// ------------------------------------------------------------

/**
 * Enables TWAI receive and future verified-transmission operations.
 *
 * Enabling TWAI does not bypass the verified-mapping gate.
 */
esp_err_t singlecan_enable_can(void);

/**
 * Disables TWAI receive and transmission operations.
 */
esp_err_t singlecan_disable_can(void);

// ------------------------------------------------------------
// TWAI STATUS
// ------------------------------------------------------------

/**
 * Writes a null-terminated TWAI status description into the supplied
 * output buffer.
 */
void singlecan_get_status(
    char *output,
    size_t output_length
);

// ------------------------------------------------------------
// VERIFIED CAN TRANSMISSION
// ------------------------------------------------------------

/**
 * Transmits one previously reviewed and compiled-in vehicle mapping.
 *
 * The caller selects only a verified mapping identifier. The CAN ID,
 * DLC, payload, and transmission policy remain private to
 * singlecan_can.c.
 *
 * SINGLECAN_VERIFIED_COMMAND_NONE is always rejected.
 *
 * Transmission is permitted only while CAN is enabled and the
 * supervised TWAI subsystem is explicitly in its RUNNING state.
 *
 * Until verified Honda mappings are added, every call that reaches
 * mapping lookup returns ESP_ERR_NOT_SUPPORTED and no call reaches
 * twai_transmit().
 */
esp_err_t singlecan_send_verified(
    singlecan_verified_command_t command
);

// ------------------------------------------------------------
// TWAI RECEIVE-DRAIN SUPPORT
// ------------------------------------------------------------

/**
 * Receives one TWAI frame so the hardware receive queue remains
 * drained and controller health monitoring remains functional.
 *
 * Raw received frames are not logged, serialized, queued, or
 * forwarded over Bluetooth. PCAN hardware and PCAN-Explorer 7 remain
 * responsible for CAN capture and decoding.
 */
esp_err_t singlecan_receive(
    twai_message_t *message
);

#ifdef __cplusplus
}
#endif