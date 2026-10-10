#include "bt_peer_authorization.h"

#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "nvs.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG =
    "BT_PEER_AUTH";

static const char *NVS_NAMESPACE =
    "bt_peer_auth";

static const char *NVS_TRUSTED_PEER_KEY =
    "trusted_peer";

static int address_is_zero(
    const esp_bd_addr_t address
)
{
    if (address == NULL) {
        return 1;
    }

    static const esp_bd_addr_t zero_address = {
        0,
        0,
        0,
        0,
        0,
        0
    };

    return memcmp(
        address,
        zero_address,
        sizeof(esp_bd_addr_t)
    ) == 0;
}

esp_err_t bt_peer_authorization_load_trusted_address(
    esp_bd_addr_t trusted_address
)
{
    if (trusted_address == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle =
        0;

    const esp_err_t open_result =
        nvs_open(
            NVS_NAMESPACE,
            NVS_READONLY,
            &handle
        );

    if (open_result != ESP_OK) {
        return open_result;
    }

    size_t stored_length =
        sizeof(esp_bd_addr_t);

    const esp_err_t read_result =
        nvs_get_blob(
            handle,
            NVS_TRUSTED_PEER_KEY,
            trusted_address,
            &stored_length
        );

    nvs_close(
        handle
    );

    if (read_result != ESP_OK) {
        return read_result;
    }

    if (
        stored_length !=
            sizeof(esp_bd_addr_t) ||
        address_is_zero(
            trusted_address
        )
    ) {
        memset(
            trusted_address,
            0,
            sizeof(esp_bd_addr_t)
        );

        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t bt_peer_authorization_store_trusted_address(
    const esp_bd_addr_t trusted_address
)
{
    if (
        trusted_address == NULL ||
        address_is_zero(
            trusted_address
        )
    ) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle =
        0;

    esp_err_t result =
        nvs_open(
            NVS_NAMESPACE,
            NVS_READWRITE,
            &handle
        );

    if (result != ESP_OK) {
        return result;
    }

    result =
        nvs_set_blob(
            handle,
            NVS_TRUSTED_PEER_KEY,
            trusted_address,
            sizeof(esp_bd_addr_t)
        );

    if (result == ESP_OK) {
        result =
            nvs_commit(
                handle
            );
    }

    nvs_close(
        handle
    );

    return result;
}

// ------------------------------------------------------------
// AUTHORIZATION INITIALIZATION
// ------------------------------------------------------------

esp_err_t bt_peer_authorization_init(void)
{
    const int bonded_device_count =
        esp_bt_gap_get_bond_device_num();

    if (bonded_device_count < 0) {
        ESP_LOGE(
            TAG,
            "Unable to query the Bluetooth bonded-device database"
        );

        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "Bluetooth trusted-peer store ready; bonded devices=%d",
        bonded_device_count
    );

    return ESP_OK;
}

int bt_peer_authorization_is_explicitly_trusted(
    const esp_bd_addr_t peer_address
)
{
    if (
        peer_address == NULL ||
        address_is_zero(
            peer_address
        )
    ) {
        ESP_LOGW(
            TAG,
            "Explicit peer authorization rejected an invalid address"
        );

        return 0;
    }

    esp_bd_addr_t trusted_address = {
        0
    };

    const esp_err_t load_result =
        bt_peer_authorization_load_trusted_address(
            trusted_address
        );

    if (load_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Explicit peer authorization denied; "
            "trusted address unavailable"
        );

        return 0;
    }

    if (
        memcmp(
            peer_address,
            trusted_address,
            sizeof(esp_bd_addr_t)
        ) != 0
    ) {
        ESP_LOGW(
            TAG,
            "Explicit peer authorization denied an address mismatch"
        );

        return 0;
    }

    return bt_peer_authorization_is_trusted(
        peer_address
    );
}

// ------------------------------------------------------------
// BONDED-DEVICE AUTHORIZATION
// ------------------------------------------------------------

int bt_peer_authorization_is_trusted(
    const esp_bd_addr_t peer_address
)
{
    if (
        peer_address == NULL ||
        address_is_zero(
            peer_address
        )
    ) {
        ESP_LOGW(
            TAG,
            "Peer authorization rejected an invalid address"
        );

        return 0;
    }

    int bonded_device_count =
        esp_bt_gap_get_bond_device_num();

    if (bonded_device_count <= 0) {
        ESP_LOGW(
            TAG,
            "Peer authorization denied; no trusted bonded devices"
        );

        return 0;
    }

    esp_bd_addr_t *bonded_devices =
        calloc(
            (size_t)bonded_device_count,
            sizeof(esp_bd_addr_t)
        );

    if (bonded_devices == NULL) {
        ESP_LOGE(
            TAG,
            "Peer authorization failed to allocate bond-list storage"
        );

        return 0;
    }

    int returned_device_count =
        bonded_device_count;

    const esp_err_t list_result =
        esp_bt_gap_get_bond_device_list(
            &returned_device_count,
            bonded_devices
        );

    if (
        list_result != ESP_OK ||
        returned_device_count < 0 ||
        returned_device_count >
            bonded_device_count
    ) {
        ESP_LOGE(
            TAG,
            "Peer authorization could not read the bond list: %s",
            esp_err_to_name(
                list_result
            )
        );

        free(
            bonded_devices
        );

        return 0;
    }

    int trusted =
        0;

    for (
        int index = 0;
        index < returned_device_count;
        ++index
    ) {
        if (
            memcmp(
                peer_address,
                bonded_devices[index],
                sizeof(esp_bd_addr_t)
            ) == 0
        ) {
            trusted =
                1;

            break;
        }
    }

    free(
        bonded_devices
    );

    if (!trusted) {
        ESP_LOGW(
            TAG,
            "Peer authorization denied an untrusted Bluetooth address"
        );
    }

    return trusted;
}
