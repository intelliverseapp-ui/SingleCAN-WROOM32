idf_component_register(
    SRCS
        "test_bt_spp_framer.c"
        "../../../main/bt_spp_framer.c"

    INCLUDE_DIRS
        "."
        "../../../main"

    REQUIRES
        unity
)
