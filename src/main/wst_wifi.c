// wst_wifi.c — WiFi provisioning (SoftAP) and STA connection

#include "wst_main.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"

static const char *TAG = "wst_wifi";

EventGroupHandle_t g_wifi_event_group = NULL;
static int s_retry_count = 0;
#define MAX_RETRY 5

// ---------------------------------------------------------------------------
// Event handler
// ---------------------------------------------------------------------------
static void event_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (base == WIFI_PROV_EVENT) {
        switch (id) {
        case WIFI_PROV_START:
            ESP_LOGI(TAG, "provisioning started — connect to AP \"%s\"",
                     WST_DEVICE_NAME);
            break;
        case WIFI_PROV_CRED_RECV: {
            wifi_sta_config_t *cfg = (wifi_sta_config_t *)data;
            ESP_LOGI(TAG, "received credentials  ssid=\"%s\"",
                     (const char *)cfg->ssid);
            break;
        }
        case WIFI_PROV_CRED_FAIL: {
            wifi_prov_sta_fail_reason_t *reason =
                (wifi_prov_sta_fail_reason_t *)data;
            ESP_LOGE(TAG, "provisioning failed: %s",
                     (*reason == WIFI_PROV_STA_AUTH_ERROR)
                         ? "auth error" : "AP not found");
            break;
        }
        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "provisioning successful");
            break;
        case WIFI_PROV_END:
            wifi_prov_mgr_deinit();
            break;
        default:
            break;
        }
    } else if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
            esp_wifi_connect();
            break;
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *ev =
                (wifi_event_ap_staconnected_t *)data;
            ESP_LOGI(TAG, "station connected (AID=%d)", ev->aid);
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "station disconnected from AP");
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "connected  ip=" IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(g_wifi_event_group, WST_WIFI_CONNECTED_BIT);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
esp_err_t wst_wifi_init(void)
{
    g_wifi_event_group = xEventGroupCreate();

    // NVS (required by WiFi and provisioning)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Network stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    // Initialize WiFi
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    // Initialize provisioning manager (SoftAP transport)
    wifi_prov_mgr_config_t prov_cfg = {
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_cfg));

    // Check if device is already provisioned
    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    if (!provisioned) {
        ESP_LOGI(TAG, "not provisioned — starting SoftAP provisioning");

        // mDNS for SoftAP service discovery
        ESP_ERROR_CHECK(mdns_init());
        ESP_ERROR_CHECK(mdns_hostname_set(WST_DEVICE_NAME));

        // No encryption on the provisioning data (Security 0)
        // For production, use WIFI_PROV_SECURITY_1 with a PoP string
        wifi_prov_security_t security = WIFI_PROV_SECURITY_0;

        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(
            security,           // security mode
            NULL,               // proof of possession (NULL for sec0)
            WST_DEVICE_NAME,    // SoftAP SSID
            WST_PROV_AP_PASSWORD // SoftAP password
        ));

        // Provisioning runs asynchronously; wait for result
        ESP_LOGI(TAG, "waiting for provisioning to complete...");
        ESP_LOGI(TAG, "connect to WiFi \"%s\" password \"%s\"",
                 WST_DEVICE_NAME, WST_PROV_AP_PASSWORD);
        ESP_LOGI(TAG, "then use ESP SoftAP Prov app or:");
        ESP_LOGI(TAG, "  python3 $IDF_PATH/tools/esp_prov.py \\");
        ESP_LOGI(TAG, "    --transport softap \\");
        ESP_LOGI(TAG, "    --ssid <YOUR_WIFI> --passphrase <YOUR_PASS>");

        xEventGroupWaitBits(g_wifi_event_group,
                            WST_WIFI_CONNECTED_BIT | WST_WIFI_FAIL_BIT,
                            pdTRUE, pdFALSE, portMAX_DELAY);
    } else {
        ESP_LOGI(TAG, "already provisioned, connecting...");
        wifi_prov_mgr_deinit();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

        xEventGroupWaitBits(g_wifi_event_group,
                            WST_WIFI_CONNECTED_BIT | WST_WIFI_FAIL_BIT,
                            pdFALSE, pdFALSE, portMAX_DELAY);
    }

EventBits_t bits = xEventGroupGetBits(g_wifi_event_group);
    if (bits & WST_WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi ready");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "WiFi connection failed");
    return ESP_FAIL;
}
