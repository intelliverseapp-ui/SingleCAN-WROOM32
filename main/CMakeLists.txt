idf_component_register(
    SRCS
        "main.c"
        "tasks.c"
        "singlecan_can.c"
        "singlecan_commands.c"
        "singlecan_leds.c"
        "bt_spp.c"
        "bt_spp_framer.c"

    INCLUDE_DIRS
        "."

    PRIV_REQUIRES
        driver
        nvs_flash
        freertos
        bt
        json
)