idf_component_register(
    SRCS
        "test_twai_supervisor.c"
        "../../../main/tasks.c"

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
        "-Wl,--wrap=twai_reconfigure_alerts"
        "-Wl,--wrap=twai_read_alerts"
        "-Wl,--wrap=twai_get_status_info"
        "-Wl,--wrap=twai_initiate_recovery"
        "-Wl,--wrap=twai_start"
)
