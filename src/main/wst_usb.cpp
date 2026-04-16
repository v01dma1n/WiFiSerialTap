// wst_usb.cpp — USB Host CDC-ACM / VCP driver
// Follows the API from the ESP-IDF cdc_acm_vcp example

#include <stdio.h>
#include <string.h>
#include <memory>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "usb/cdc_acm_host.h"
#include "usb/vcp_ch34x.hpp"
#include "usb/vcp_cp210x.hpp"
#include "usb/vcp_ftdi.hpp"
#include "usb/vcp.hpp"
#include "usb/usb_host.h"

extern "C" {
#include "wst_main.h"
}

using namespace esp_usb;

static const char *TAG = "wst_usb";

static wst_usb_rx_cb_t s_rx_cb = NULL;
static SemaphoreHandle_t s_device_disconnected_sem = NULL;
static CdcAcmDevice *s_active_dev = NULL;

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------
static bool handle_rx(const uint8_t *data, size_t data_len, void *arg)
{
    if (s_rx_cb && data_len > 0) {
        s_rx_cb(data, data_len);
    }
    return true;
}

static void handle_event(const cdc_acm_host_dev_event_data_t *event,
                         void *user_ctx)
{
    switch (event->type) {
    case CDC_ACM_HOST_ERROR:
        ESP_LOGE(TAG, "CDC-ACM error, err_no = %d", event->data.error);
        break;
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
        ESP_LOGW(TAG, "USB device disconnected");
        s_active_dev = NULL;
        xSemaphoreGive(s_device_disconnected_sem);
        break;
    case CDC_ACM_HOST_SERIAL_STATE:
        ESP_LOGI(TAG, "serial state 0x%04X", event->data.serial_state.val);
        break;
    case CDC_ACM_HOST_NETWORK_CONNECTION:
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// USB host library task
// ---------------------------------------------------------------------------
static void usb_lib_task(void *arg)
{
    while (true) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_ERROR_CHECK(usb_host_device_free_all());
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "all USB devices freed");
        }
    }
}

// ---------------------------------------------------------------------------
// VCP connection loop task
// ---------------------------------------------------------------------------
static void vcp_task(void *arg)
{
    const cdc_acm_host_device_config_t dev_config = {
        .connection_timeout_ms = 5000,
        .out_buffer_size = 512,
        .in_buffer_size = 512,
        .event_cb = handle_event,
        .data_cb = handle_rx,
        .user_arg = NULL,
    };

    while (true) {
        ESP_LOGI(TAG, "waiting for USB VCP device...");
        auto vcp = std::unique_ptr<CdcAcmDevice>(VCP::open(&dev_config));

        if (vcp == nullptr) {
            ESP_LOGI(TAG, "no VCP device found, retrying...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(10));

        ESP_LOGI(TAG, "USB VCP device opened, configuring 115200/8N1");
        cdc_acm_line_coding_t line_coding = {
            .dwDTERate = 115200,
            .bCharFormat = 0,   // 1 stop bit
            .bParityType = 0,   // no parity
            .bDataBits = 8,
        };
        esp_err_t err = vcp->line_coding_set(&line_coding);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "line_coding_set: %s (non-fatal)",
                     esp_err_to_name(err));
        }

        err = vcp->set_control_line_state(true, true);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "set_control_line_state: %s (non-fatal)",
                     esp_err_to_name(err));
        }

        // Store raw pointer for tx from other tasks
        s_active_dev = vcp.get();

        ESP_LOGI(TAG, "device ready, streaming data");
        xSemaphoreTake(s_device_disconnected_sem, portMAX_DELAY);

        s_active_dev = NULL;
        ESP_LOGI(TAG, "device gone, will wait for next one");
    }
}

// ---------------------------------------------------------------------------
// Public API (C linkage)
// ---------------------------------------------------------------------------
extern "C" void wst_usb_set_rx_callback(wst_usb_rx_cb_t cb)
{
    s_rx_cb = cb;
}

extern "C" esp_err_t wst_usb_send(const uint8_t *data, size_t len)
{
    if (!s_active_dev) return ESP_ERR_INVALID_STATE;
    return s_active_dev->tx_blocking(const_cast<uint8_t *>(data), len, 1000);
}

extern "C" esp_err_t wst_usb_init(void)
{
    s_device_disconnected_sem = xSemaphoreCreateBinary();
    assert(s_device_disconnected_sem);

    // Install USB host library
    ESP_LOGI(TAG, "installing USB Host");
    usb_host_config_t host_config = {};
    host_config.skip_phy_setup = false;
    host_config.intr_flags = ESP_INTR_FLAG_LEVEL1;
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    // USB host event task
    xTaskCreatePinnedToCore(usb_lib_task, "usb_lib", 4096, NULL, 10, NULL, 0);

    // Install CDC-ACM driver
    ESP_LOGI(TAG, "installing CDC-ACM driver");
    ESP_ERROR_CHECK(cdc_acm_host_install(NULL));

    // Register VCP drivers
    VCP::register_driver<FT23x>();
    VCP::register_driver<CP210x>();
    VCP::register_driver<CH34x>();

    // VCP connection loop task
    xTaskCreatePinnedToCore(vcp_task, "vcp_task", 4096, NULL, 3, NULL, 0);

    ESP_LOGI(TAG, "USB host initialized");
    return ESP_OK;
}
