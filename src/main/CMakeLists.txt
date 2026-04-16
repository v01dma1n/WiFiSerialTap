idf_component_register(
    SRCS "main.c" "wst_wifi.c" "wst_usb.cpp" "wst_telnet.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_http_server
        esp_event
        wifi_provisioning
)