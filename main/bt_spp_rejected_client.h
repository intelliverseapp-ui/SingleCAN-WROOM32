#pragma once

#include "esp_err.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*bt_spp_rejected_client_disconnect_t)(
    uint32_t handle,
    esp_err_t *result
);

/**
* Initializes bounded rejected-client disconnect recovery.
*
* disconnect_callback atomically protects authorized-session
* ownership and conditionally performs the stack disconnect request.
*
* The callback returns nonzero when a disconnect request was issued
* and stores its ESP-IDF result through result.
*
* The callback returns zero when the handle is protected and must not
* be disconnected.
*/
esp_err_t bt_spp_rejected_client_init(
    bt_spp_rejected_client_disconnect_t disconnect_callback
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
