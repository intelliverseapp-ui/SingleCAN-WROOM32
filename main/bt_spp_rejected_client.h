#pragma once

#include "esp_err.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*bt_spp_rejected_client_disconnect_t)(
    uint32_t handle
);

typedef uint32_t (*bt_spp_rejected_client_active_handle_t)(
    void
);

/**
* Initializes bounded rejected-client disconnect recovery.
*
* disconnect_callback performs the stack disconnect request.
* active_handle_callback returns the authorized active-session handle.
*/
esp_err_t bt_spp_rejected_client_init(
    bt_spp_rejected_client_disconnect_t disconnect_callback,
    bt_spp_rejected_client_active_handle_t active_handle_callback
);

/**
* Rejects one non-authorized SPP handle.
*
* The first disconnect attempt is immediate. Failed requests enter a
* bounded asynchronous retry path.
*/
void bt_spp_rejected_client_reject(
    uint32_t handle
);

/**
* Cancels pending retry work after the stack reports that a rejected
* or stale handle closed.
*/
void bt_spp_rejected_client_on_closed(
    uint32_t handle
);

#ifdef __cplusplus
}
#endif
