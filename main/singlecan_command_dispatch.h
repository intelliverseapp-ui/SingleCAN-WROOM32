#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SINGLECAN_COMMAND_DISPATCH_NOT_IMPLEMENTED = 0,
    SINGLECAN_COMMAND_DISPATCH_UNSUPPORTED
} singlecan_command_dispatch_result_t;

/**
 * Finds and invokes one allowlisted vehicle-command handler.
 *
 * The current handlers are non-transmitting stubs. A recognized
 * command therefore returns NOT_IMPLEMENTED after invoking its safe
 * handler. Unknown or unsafe commands return UNSUPPORTED.
 */
singlecan_command_dispatch_result_t
singlecan_command_dispatch(
    const char *command
);

#ifdef __cplusplus
}
#endif
