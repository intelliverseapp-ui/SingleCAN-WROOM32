idf_component_register(
    SRCS
        "main.c"
        "http_server.c"
        "wifi_ap.c"
        "tasks.c"
        "singlecan_leds.c"
        "singlecan_can.c"
        "singlecan_commands.c"
        "tcp_server.c"
        "tcp_queue.c"

    INCLUDE_DIRS
        "."

    PRIV_REQUIRES
        driver
        nvs_flash
        esp_wifi
        esp_netif
        esp_http_server
        freertos
        lwip
)
