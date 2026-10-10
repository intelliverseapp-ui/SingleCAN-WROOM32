#pragma once

#include "esp_err.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef SINGLECAN_SPP_COORDINATOR_TEST

esp_err_t bt_spp_session_lifecycle_init(void);

int bt_spp_accept_session_transition(
    uint32_t handle,
    uint32_t *session_id
);

int bt_spp_close_session_transition(
    uint32_t handle
);

int bt_spp_force_close_session_transition(
    uint32_t handle,
    uint32_t session_id
);

int bt_spp_disconnect_rejected_client(
    uint32_t handle,
    esp_err_t *result
);

#endif

#ifdef __cplusplus
}
#endif
