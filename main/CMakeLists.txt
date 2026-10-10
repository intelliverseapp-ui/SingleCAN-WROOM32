idf_component_register(
    SRCS
        "main.c"
        "tasks.c"
        "singlecan_can.c"
        "singlecan_commands.c"
        "singlecan_command_handlers.c"
        "singlecan_command_dispatch.c"
        "singlecan_responses.c"
        "singlecan_leds.c"
        "bt_spp.c"
        "bt_spp_framer.c"
        "bt_spp_writer.c"
        "bt_spp_session.c"
        "bt_peer_authorization.c"

    INCLUDE_DIRS
        "."

    PRIV_REQUIRES
        driver
        nvs_flash
        freertos
        bt
        json
)