#pragma once

#include "esp_bt_defs.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initializes peer authorization.
 *
 * The ESP-IDF Classic Bluetooth bonded-device database is used as
 * the trusted-peer store.
 */
esp_err_t bt_peer_authorization_init(void);

/**
 * Returns nonzero only when peer_address exactly matches a device in
 * the Classic Bluetooth bonded-device database.
 *
 * Authorization fails closed for:
 * - A null address
 * - An all-zero address
 * - No bonded devices
 * - Bond-database query errors
 * - Allocation failures
 * - An address absent from the bond database
 */
int bt_peer_authorization_is_trusted(
    const esp_bd_addr_t peer_address
);

#ifdef __cplusplus
}
#endif
