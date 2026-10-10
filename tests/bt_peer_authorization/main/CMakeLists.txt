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
        nvs_flash
)

target_link_libraries(
    ${COMPONENT_LIB}
    INTERFACE
        "-Wl,--wrap=esp_bt_gap_get_bond_device_num"
        "-Wl,--wrap=esp_bt_gap_get_bond_device_list"
        "-Wl,--wrap=nvs_open"
        "-Wl,--wrap=nvs_get_blob"
        "-Wl,--wrap=nvs_set_blob"
        "-Wl,--wrap=nvs_commit"
        "-Wl,--wrap=nvs_close"
)
