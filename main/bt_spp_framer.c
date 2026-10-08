#include "bt_spp_framer.h"

#include "esp_log.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char *TAG =
    "BT_SPP_FRAMER";

#define BT_SPP_MAX_FRAME_LENGTH 4096

#define BT_SPP_RX_BUFFER_SIZE \
    (BT_SPP_MAX_FRAME_LENGTH + 1)

static bt_spp_frame_handler_t s_frame_handler =
    NULL;

static char s_rx_buffer[
    BT_SPP_RX_BUFFER_SIZE
];

static size_t s_rx_length =
    0;

static int s_discard_until_newline =
    0;

static int s_pending_carriage_return =
    0;

// ------------------------------------------------------------
// INTERNAL HELPERS
// ------------------------------------------------------------

static int byte_is_allowed(
    uint8_t byte
)
{
    if (
        byte == '\n' ||
        byte == '\r'
    ) {
        return 1;
    }

    if (
        byte >= 0x20 &&
        byte <= 0x7E
    ) {
        return 1;
    }

    return 0;
}

static void discard_current_frame(
    const char *reason
)
{
    ESP_LOGW(
        TAG,
        "Discarding invalid SPP frame: %s",
        reason
    );

    memset(
        s_rx_buffer,
        0,
        sizeof(s_rx_buffer)
    );

    s_rx_length =
        0;

    s_pending_carriage_return =
        0;

    s_discard_until_newline =
        1;
}

static void deliver_complete_frame(void)
{
    if (s_rx_length == 0) {
        return;
    }

    s_rx_buffer[
        s_rx_length
    ] = '\0';

    ESP_LOGI(
        TAG,
        "Complete SPP frame assembled, len=%zu",
        s_rx_length
    );

    if (s_frame_handler != NULL) {
        s_frame_handler(
            s_rx_buffer
        );
    }

    memset(
        s_rx_buffer,
        0,
        sizeof(s_rx_buffer)
    );

    s_rx_length =
        0;
}

// ------------------------------------------------------------
// PUBLIC FRAMER API
// ------------------------------------------------------------

int bt_spp_framer_init(
    bt_spp_frame_handler_t handler
)
{
    if (handler == NULL) {
        ESP_LOGE(
            TAG,
            "Cannot initialize framer without a frame handler"
        );

        return 0;
    }

    s_frame_handler =
        handler;

    bt_spp_framer_reset();

    ESP_LOGI(
        TAG,
        "Bounded SPP receive framer initialized"
    );

    return 1;
}

void bt_spp_framer_reset(void)
{
    memset(
        s_rx_buffer,
        0,
        sizeof(s_rx_buffer)
    );

    s_rx_length =
        0;

    s_discard_until_newline =
        0;

    s_pending_carriage_return =
        0;
}

void bt_spp_framer_process(
    const uint8_t *data,
    size_t data_length
)
{
    if (
        data == NULL ||
        data_length == 0
    ) {
        ESP_LOGW(
            TAG,
            "SPP data fragment contained no data"
        );

        return;
    }

    for (
        size_t index = 0;
        index < data_length;
        ++index
    ) {
        const uint8_t byte =
            data[index];

        if (s_discard_until_newline) {
            if (byte == '\n') {
                s_discard_until_newline =
                    0;

                s_rx_length =
                    0;

                s_pending_carriage_return =
                    0;

                memset(
                    s_rx_buffer,
                    0,
                    sizeof(s_rx_buffer)
                );

                ESP_LOGI(
                    TAG,
                    "SPP frame discard completed at newline"
                );
            }

            continue;
        }

        /*
         * A carriage return is valid only as part of CRLF framing.
         * The pending state persists across fragmented callbacks.
         */
        if (s_pending_carriage_return) {
            s_pending_carriage_return =
                0;

            if (byte == '\n') {
                deliver_complete_frame();

                continue;
            }

            discard_current_frame(
                "carriage return outside CRLF terminator"
            );

            continue;
        }

        if (!byte_is_allowed(byte)) {
            discard_current_frame(
                "binary or control byte detected"
            );

            continue;
        }

        if (byte == '\r') {
            s_pending_carriage_return =
                1;

            continue;
        }

        if (byte == '\n') {
            deliver_complete_frame();

            continue;
        }

        if (
            s_rx_length >=
            BT_SPP_MAX_FRAME_LENGTH
        ) {
            discard_current_frame(
                "maximum frame length exceeded"
            );

            continue;
        }

        s_rx_buffer[
            s_rx_length
        ] = (char)byte;

        s_rx_length +=
            1;
    }
}