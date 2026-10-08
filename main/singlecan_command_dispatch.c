#include "singlecan_command_dispatch.h"

#include "singlecan_command_handlers.h"

#include "esp_log.h"

#include <stddef.h>
#include <string.h>
#include <strings.h>

static const char *TAG =
    "SingleCAN_DISPATCH";

#define MAXIMUM_COMMAND_LENGTH 64

typedef void (*command_handler_t)(void);

typedef struct {
    const char *command;
    command_handler_t handler;
} command_mapping_t;

// ------------------------------------------------------------
// COMMAND ALLOWLIST
// ------------------------------------------------------------

static const command_mapping_t COMMAND_MAPPINGS[] = {
    {
        "LOCK_DOORS",
        singlecan_cmd_lock_doors
    },
    {
        "locks.all.lock",
        singlecan_cmd_lock_doors
    },
    {
        "UNLOCK_DOORS",
        singlecan_cmd_unlock_doors
    },
    {
        "locks.all.unlock",
        singlecan_cmd_unlock_doors
    },
    {
        "WINDOW_DRIVER_DOWN",
        singlecan_cmd_windows_down
    },
    {
        "WINDOW_PASSENGER_DOWN",
        singlecan_cmd_windows_down
    },
    {
        "WINDOWS_DOWN",
        singlecan_cmd_windows_down
    },
    {
        "WINDOW_DRIVER_UP",
        singlecan_cmd_windows_up
    },
    {
        "WINDOW_PASSENGER_UP",
        singlecan_cmd_windows_up
    },
    {
        "WINDOWS_UP",
        singlecan_cmd_windows_up
    },
    {
        "SUNROOF_OPEN",
        singlecan_cmd_sunroof_open
    },
    {
        "MOONROOF_OPEN",
        singlecan_cmd_sunroof_open
    },
    {
        "SUNROOF_CLOSE",
        singlecan_cmd_sunroof_close
    },
    {
        "MOONROOF_CLOSE",
        singlecan_cmd_sunroof_close
    },
    {
        "SUNROOF_VENT",
        singlecan_cmd_sunroof_vent
    },
    {
        "HEADLIGHTS_ON",
        singlecan_cmd_headlights_on
    },
    {
        "HEADLIGHTS_OFF",
        singlecan_cmd_headlights_off
    },
    {
        "FOG_LIGHTS_ON",
        singlecan_cmd_fog_lights_on
    },
    {
        "FOG_LIGHTS_OFF",
        singlecan_cmd_fog_lights_off
    },
    {
        "INTERIOR_LIGHTS_ON",
        singlecan_cmd_interior_lights_on
    },
    {
        "INTERIOR_LIGHTS_OFF",
        singlecan_cmd_interior_lights_off
    },
    {
        "AC_ON",
        singlecan_cmd_ac_on
    },
    {
        "AC_OFF",
        singlecan_cmd_ac_off
    },
    {
        "FAN_UP",
        singlecan_cmd_fan_up
    },
    {
        "FAN_DOWN",
        singlecan_cmd_fan_down
    },
    {
        "AUDIO_MUTE",
        singlecan_cmd_audio_mute
    },
    {
        "AUDIO_UNMUTE",
        singlecan_cmd_audio_unmute
    },
    {
        "TRUNK_OPEN",
        singlecan_cmd_trunk_open
    },
    {
        "HORN",
        singlecan_cmd_horn
    },
    {
        "HORN_SHORT",
        singlecan_cmd_horn
    },
    {
        "HAZARDS_ON",
        singlecan_cmd_hazards_on
    },
    {
        "HAZARDS_OFF",
        singlecan_cmd_hazards_off
    },
    {
        "DEFROST_ON",
        singlecan_cmd_defrost_on
    },
    {
        "DEFROST_FRONT",
        singlecan_cmd_defrost_on
    },
    {
        "DEFROST_REAR",
        singlecan_cmd_defrost_on
    },
    {
        "DEFROST_OFF",
        singlecan_cmd_defrost_off
    },
    {
        "DEFROST_REAR_OFF",
        singlecan_cmd_defrost_off
    }
};

static const size_t COMMAND_MAPPING_COUNT =
    sizeof(COMMAND_MAPPINGS) /
    sizeof(COMMAND_MAPPINGS[0]);


// ------------------------------------------------------------
// COMMAND STRING VALIDATION
// ------------------------------------------------------------

static int command_string_is_safe(
    const char *text
)
{
    if (
        text == NULL ||
        text[0] == '\0'
    ) {
        return 0;
    }

    const size_t length =
        strlen(
            text
        );

    if (
        length == 0 ||
        length >
            MAXIMUM_COMMAND_LENGTH
    ) {
        return 0;
    }

    for (
        size_t index = 0;
        index < length;
        ++index
    ) {
        const unsigned char character =
            (unsigned char)text[index];

        if (
            character < 0x20U ||
            character == 0x7FU
        ) {
            return 0;
        }
    }

    return 1;
}

// ------------------------------------------------------------
// PUBLIC CANONICAL COMMAND DISPATCHER
// ------------------------------------------------------------

singlecan_command_dispatch_result_t
singlecan_command_dispatch(
    const char *command
)
{
    if (
        !command_string_is_safe(
            command
        )
    ) {
        return
            SINGLECAN_COMMAND_DISPATCH_UNSUPPORTED;
    }

    for (
        size_t index = 0;
        index < COMMAND_MAPPING_COUNT;
        ++index
    ) {
        if (
            strcasecmp(
                command,
                COMMAND_MAPPINGS[index].command
            ) == 0
        ) {
            ESP_LOGI(
                TAG,
                "Recognized allowlisted command"
            );

            COMMAND_MAPPINGS[index]
                .handler();

            return
                SINGLECAN_COMMAND_DISPATCH_NOT_IMPLEMENTED;
        }
    }

    ESP_LOGW(
        TAG,
        "Unsupported canonical command"
    );

    return
        SINGLECAN_COMMAND_DISPATCH_UNSUPPORTED;
}
