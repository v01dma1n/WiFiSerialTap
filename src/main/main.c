// main.c — WiFiSerialTap for ESP32-S3
//
// USB-inline serial sniffer: enumerates a USB CDC/VCP device (CP210x,
// CH340, FTDI) on the USB OTG port, relays its serial output over WiFi
// via a Telnet server, and mirrors to the UART console.
//
// WiFi credentials are provisioned via SoftAP on first boot — no
// hardcoded SSID/password.

#include "wst_main.h"
#include <string.h>

static const char *TAG = "wst";

// ---------------------------------------------------------------------------
// USB RX callback — called from the VCP driver context
// Mirror data to UART console and forward to Telnet client.
// ---------------------------------------------------------------------------
static void on_usb_rx(const uint8_t *data, size_t len)
{
    // Local mirror (UART console)
    fwrite(data, 1, len, stdout);
    fflush(stdout);

    // Network relay
    wst_telnet_send(data, len);
}

// ---------------------------------------------------------------------------
// Reverse channel task — Telnet client → USB device
// ---------------------------------------------------------------------------
static void reverse_channel_task(void *arg)
{
    uint8_t buf[256];
    while (true) {
        size_t n = wst_telnet_recv(buf, sizeof(buf));
        if (n > 0) {
            wst_usb_send(buf, n);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ---------------------------------------------------------------------------
// app_main
// ---------------------------------------------------------------------------
void app_main(void)
{
    ESP_LOGI(TAG, "WiFiSerialTap starting");

    // 1. WiFi — provision or connect
    esp_err_t err = wst_wifi_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed, restarting in 5s...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    // 2. Telnet server
    ESP_ERROR_CHECK(wst_telnet_init(WST_TELNET_PORT));

    // 3. USB host — install drivers, start VCP connection loop
    wst_usb_set_rx_callback(on_usb_rx);
    ESP_ERROR_CHECK(wst_usb_init());

    // 4. Reverse channel (Telnet → USB)
    xTaskCreate(reverse_channel_task, "reverse_ch", 4096, NULL, 2, NULL);

    ESP_LOGI(TAG, "all systems running");
}
