idf_component_register(
    SRCS
        "test_can_invariant.c"
        "../../../main/singlecan_can.c"

    INCLUDE_DIRS
        "."
        "../../../main"

    REQUIRES
        unity
        driver
        freertos
)

target_link_libraries(
    ${COMPONENT_LIB}
    INTERFACE
        "-Wl,--wrap=twai_transmit"
)
