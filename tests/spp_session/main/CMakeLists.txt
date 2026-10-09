idf_component_register(
    SRCS
        "test_bt_spp_session.c"
        "../../../main/bt_spp_session.c"

    INCLUDE_DIRS
        "."
        "../../../main"

    REQUIRES
        unity
        bt
        freertos
)
