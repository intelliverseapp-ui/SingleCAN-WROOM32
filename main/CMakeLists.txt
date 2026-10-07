idf_component_register(
    SRCS
        "main.c"
        "tasks.c"
        "singlecan_can.c"
        "singlecan_commands.c"
        "tcp_queue.c"
        "singlecan_leds.c"
        "bt_spp.c"

    INCLUDE_DIRS
        "."

    PRIV_REQUIRES
        driver
        nvs_flash
        freertos
        lwip
        bt
        json
)