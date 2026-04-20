#ifndef WST_MAIN_H
#define WST_MAIN_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define WST_DEVICE_NAME       "WiFiSerialTap"
#define WST_TELNET_PORT       23
#define WST_OTA_PORT          8080
#define WST_USB_RX_BUF_SIZE   512
#define WST_PROV_AP_PASSWORD  "wstconfig"

// WST_OTA_KEY is injected at compile time by CMake from the
// WST_OTA_KEY environment variable.  See src/main/CMakeLists.txt.

// Event group bits
#define WST_WIFI_CONNECTED_BIT  BIT0
#define WST_WIFI_FAIL_BIT       BIT1

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------
extern EventGroupHandle_t g_wifi_event_group;

// ---------------------------------------------------------------------------
// WiFi subsystem (wst_wifi.c)
// ---------------------------------------------------------------------------
esp_err_t wst_wifi_init(void);

// ---------------------------------------------------------------------------
// USB Host CDC subsystem (wst_usb.cpp)
// ---------------------------------------------------------------------------
esp_err_t wst_usb_init(void);

// Callback type: called when data arrives from USB device
typedef void (*wst_usb_rx_cb_t)(const uint8_t *data, size_t len);
void wst_usb_set_rx_callback(wst_usb_rx_cb_t cb);

// Send data to USB device (reverse channel)
esp_err_t wst_usb_send(const uint8_t *data, size_t len);

// ---------------------------------------------------------------------------
// Telnet server (wst_telnet.c)
// ---------------------------------------------------------------------------
esp_err_t wst_telnet_init(uint16_t port);
void wst_telnet_send(const uint8_t *data, size_t len);
size_t wst_telnet_recv(uint8_t *buf, size_t max_len);
bool wst_telnet_has_client(void);

// ---------------------------------------------------------------------------
// OTA update server (wst_ota.c)
// ---------------------------------------------------------------------------
esp_err_t wst_ota_init(void);

#endif // WST_MAIN_H
