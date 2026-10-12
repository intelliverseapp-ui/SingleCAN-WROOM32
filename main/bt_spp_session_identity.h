#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns the current SPP session generation.
 *
 * Zero means no active session identity is available.
 */
uint32_t bt_spp_session_get_id(void);

#ifdef __cplusplus
}
#endif
