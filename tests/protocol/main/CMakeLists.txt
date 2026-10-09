idf_component_register(
    SRCS
        "test_singlecan_protocol.c"
        "protocol_test_doubles.c"
        "../../../main/singlecan_commands.c"

    INCLUDE_DIRS
        "."
        "../../../main"

    REQUIRES
        unity
        json
        freertos
)
