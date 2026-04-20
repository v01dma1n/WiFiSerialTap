idf_component_register(
    SRCS "main.c" "wst_wifi.c" "wst_usb.cpp" "wst_telnet.c" "wst_ota.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_http_server
        esp_event
        wifi_provisioning
        app_update
        esp_partition
)

# OTA key: must be set via environment variable WST_OTA_KEY
# Example: WST_OTA_KEY=mysecret idf.py build
if(DEFINED ENV{WST_OTA_KEY})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        WST_OTA_KEY="$ENV{WST_OTA_KEY}"
    )
else()
    message(FATAL_ERROR
        "\n"
        "========================================\n"
        "  WST_OTA_KEY environment variable not set!\n"
        "  Build with: WST_OTA_KEY=yourkey idf.py build\n"
        "========================================\n"
    )
endif()
