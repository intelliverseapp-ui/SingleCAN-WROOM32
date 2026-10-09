#include "protocol_test_doubles.h"

#include "esp_err.h"

#include <stddef.h>
#include <string.h>

static protocol_test_response_capture_t s_response;

static int s_dispatch_call_count =
    0;

static char s_dispatched_command[80];

static singlecan_command_dispatch_result_t s_dispatch_result =
    SINGLECAN_COMMAND_DISPATCH_NOT_IMPLEMENTED;

static void copy_optional_text(
    char *destination,
    size_t destination_size,
    const char *source
)
{
    if (
        destination == NULL ||
        destination_size == 0
    ) {
        return;
    }

    destination[0] =
        '\0';

    if (source == NULL) {
        return;
    }

    strncpy(
        destination,
        source,
        destination_size - 1
    );

    destination[
        destination_size - 1
    ] = '\0';
}

void protocol_test_doubles_reset(void)
{
    memset(
        &s_response,
        0,
        sizeof(s_response)
    );

    s_response.packet_id =
        -1;

    s_dispatch_call_count =
        0;

    memset(
        s_dispatched_command,
        0,
        sizeof(s_dispatched_command)
    );

    s_dispatch_result =
        SINGLECAN_COMMAND_DISPATCH_NOT_IMPLEMENTED;
}

const protocol_test_response_capture_t *
protocol_test_response_capture(void)
{
    return &s_response;
}

int protocol_test_dispatch_call_count(void)
{
    return s_dispatch_call_count;
}

const char *protocol_test_dispatched_command(void)
{
    return s_dispatched_command;
}

void protocol_test_set_dispatch_result(
    singlecan_command_dispatch_result_t result
)
{
    s_dispatch_result =
        result;
}

esp_err_t singlecan_response_send(
    int packet_id,
    const char *status,
    const char *command,
    const char *reason
)
{
    s_response.call_count +=
        1;

    s_response.packet_id =
        packet_id;

    copy_optional_text(
        s_response.status,
        sizeof(s_response.status),
        status
    );

    copy_optional_text(
        s_response.command,
        sizeof(s_response.command),
        command
    );

    copy_optional_text(
        s_response.reason,
        sizeof(s_response.reason),
        reason
    );

    return ESP_OK;
}

singlecan_command_dispatch_result_t
singlecan_command_dispatch(
    const char *command
)
{
    s_dispatch_call_count +=
        1;

    copy_optional_text(
        s_dispatched_command,
        sizeof(s_dispatched_command),
        command
    );

    return s_dispatch_result;
}
