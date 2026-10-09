#pragma once

#include "singlecan_command_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int call_count;
    int packet_id;
    char status[32];
    char command[80];
    char reason[80];
} protocol_test_response_capture_t;

void protocol_test_doubles_reset(void);

const protocol_test_response_capture_t *
protocol_test_response_capture(void);

int protocol_test_dispatch_call_count(void);

const char *protocol_test_dispatched_command(void);

void protocol_test_set_dispatch_result(
    singlecan_command_dispatch_result_t result
);

#ifdef __cplusplus
}
#endif
