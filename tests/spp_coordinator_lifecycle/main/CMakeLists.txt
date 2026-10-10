idf_component_register(
    SRCS
        "test_bt_spp_coordinator_lifecycle.c"
        "../../../main/bt_spp.c"
        "../../../main/bt_spp_session.c"

    INCLUDE_DIRS
        "."
        "../../../main"

    REQUIRES
        unity
        bt
        freertos
)

target_compile_definitions(
    ${COMPONENT_LIB}
    PRIVATE
        SINGLECAN_SPP_COORDINATOR_TEST=1
)
