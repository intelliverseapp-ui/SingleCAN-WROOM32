idf_component_register(
    SRCS
        "test_bt_spp_writer.c"
        "../../../main/bt_spp_writer.c"

    INCLUDE_DIRS
        "."
        "../../../main"

    REQUIRES
        unity
        bt
        freertos
)
