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

/**
 * Returns nonzero only when the supplied peer:
 *
 * - Exactly matches the trusted address stored in NVS
 * - Remains present in the Bluetooth bonded-device database
 *
 * Missing, malformed, or unreadable NVS state fails closed.
 */
int bt_peer_authorization_is_explicitly_trusted(
    const esp_bd_addr_t peer_address
);

/**
 * Loads the explicitly trusted Bluetooth address from NVS.
 *
 * Returns ESP_OK only when one valid six-byte address is loaded.
 * Returns ESP_ERR_NVS_NOT_FOUND when no address has been stored.
 */
esp_err_t bt_peer_authorization_load_trusted_address(
    esp_bd_addr_t trusted_address
);

/**
 * Stores one explicitly trusted Bluetooth address in NVS.
 *
 * The address is committed before this function returns ESP_OK.
 */
esp_err_t bt_peer_authorization_store_trusted_address(
    const esp_bd_addr_t trusted_address
);

#ifdef __cplusplus
}
#endif
