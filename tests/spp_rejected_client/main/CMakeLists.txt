idf_component_register(
    SRCS
        "test_bt_spp_rejected_client.c"
        "../../../main/bt_spp_rejected_client.c"

    INCLUDE_DIRS
        "."
        "../../../main"

    REQUIRES
        unity
        freertos
)
