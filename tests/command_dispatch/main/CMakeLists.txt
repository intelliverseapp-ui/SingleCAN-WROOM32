idf_component_register(
    SRCS
        "test_command_dispatch.c"
        "../../../main/singlecan_command_dispatch.c"
        "../../../main/singlecan_command_handlers.c"

    INCLUDE_DIRS
        "."
        "../../../main"

    REQUIRES
        unity
)
