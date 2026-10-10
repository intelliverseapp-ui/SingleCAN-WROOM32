idf_component_register(
    SRCS
        "test_bt_peer_authorization.c"
        "../../../main/bt_peer_authorization.c"

    INCLUDE_DIRS
        "."
        "../../../main"

    REQUIRES
        unity
        bt
)

target_link_libraries(
    ${COMPONENT_LIB}
    INTERFACE
        "-Wl,--wrap=esp_bt_gap_get_bond_device_num"
        "-Wl,--wrap=esp_bt_gap_get_bond_device_list"
)
