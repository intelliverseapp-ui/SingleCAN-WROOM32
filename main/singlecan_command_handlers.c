#include "singlecan_command_handlers.h"

#include "esp_log.h"

static const char *TAG =
    "SingleCAN_HANDLERS";

// ------------------------------------------------------------
// NON-TRANSMITTING VEHICLE COMMAND STUBS
// ------------------------------------------------------------

#define DEFINE_COMMAND_STUB(function_name, description) \
    void function_name(void) \
    { \
        ESP_LOGI( \
            TAG, \
            "CMD recognized: " description \
            "; mapping not implemented" \
        ); \
    }

DEFINE_COMMAND_STUB(
    singlecan_cmd_lock_doors,
    "lock doors"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_unlock_doors,
    "unlock doors"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_windows_down,
    "windows down"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_windows_up,
    "windows up"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_sunroof_open,
    "sunroof open"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_sunroof_close,
    "sunroof close"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_sunroof_vent,
    "sunroof vent"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_headlights_on,
    "headlights on"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_headlights_off,
    "headlights off"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_fog_lights_on,
    "fog lights on"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_fog_lights_off,
    "fog lights off"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_interior_lights_on,
    "interior lights on"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_interior_lights_off,
    "interior lights off"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_ac_on,
    "AC on"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_ac_off,
    "AC off"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_fan_up,
    "fan up"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_fan_down,
    "fan down"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_audio_mute,
    "audio mute"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_audio_unmute,
    "audio unmute"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_trunk_open,
    "trunk open"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_horn,
    "horn"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_hazards_on,
    "hazards on"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_hazards_off,
    "hazards off"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_defrost_on,
    "defrost on"
)

DEFINE_COMMAND_STUB(
    singlecan_cmd_defrost_off,
    "defrost off"
)

#undef DEFINE_COMMAND_STUB
