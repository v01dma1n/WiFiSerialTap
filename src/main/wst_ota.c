// wst_ota.c — HTTP server endpoint for OTA firmware push
//
// Usage: curl -H "X-OTA-Key: yourkey" http://<device-ip>:8080/ota 
//             --data-binary @build/wifi_serial_tap.bin

#include "wst_main.h"
#include <string.h>
#include <inttypes.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_http_server.h"
#include "esp_app_desc.h"

static const char *TAG = "wst_ota";

#define OTA_BUF_SIZE  4096

static esp_err_t ota_post_handler(httpd_req_t *req)
{
    // Authenticate: require X-OTA-Key header
    char key_buf[64] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-OTA-Key", key_buf, sizeof(key_buf)) != ESP_OK
        || strcmp(key_buf, WST_OTA_KEY) != 0) {
        ESP_LOGW(TAG, "OTA rejected: invalid or missing X-OTA-Key");
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid OTA key");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update started, content length=%d", req->content_len);

    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "no OTA partition available");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "No OTA partition");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "writing to partition '%s' at offset 0x%" PRIx32,
             update_partition->label, update_partition->address);

    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "OTA begin failed");
        return ESP_FAIL;
    }

    char *buf = malloc(OTA_BUF_SIZE);
    if (!buf) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Out of memory");
        return ESP_FAIL;
    }

    int total_read = 0;
    int remaining = req->content_len;

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf,
                                      remaining < OTA_BUF_SIZE
                                          ? remaining : OTA_BUF_SIZE);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "receive error at byte %d", total_read);
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Receive error");
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buf, recv_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed at byte %d: %s",
                     total_read, esp_err_to_name(err));
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "OTA write failed");
            return ESP_FAIL;
        }

        total_read += recv_len;
        remaining -= recv_len;

        if (total_read % (100 * 1024) < OTA_BUF_SIZE) {
            ESP_LOGI(TAG, "progress: %d / %d bytes", total_read, req->content_len);
        }
    }

    free(buf);

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "OTA validation failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s",
                 esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Set boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update successful (%d bytes), rebooting...", total_read);
    httpd_resp_sendstr(req, "OTA OK, rebooting...\n");

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t app_desc;
    esp_ota_get_partition_description(running, &app_desc);

    char resp[256];
    snprintf(resp, sizeof(resp),
             "{ \"app\": \"%s\", \"version\": \"%s\", "
             "\"partition\": \"%s\", \"idf\": \"%s\" }\n",
             app_desc.project_name,
             app_desc.version,
             running->label,
             app_desc.idf_ver);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

esp_err_t wst_ota_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.server_port = WST_OTA_PORT;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t ota_uri = {
        .uri = "/ota",
        .method = HTTP_POST,
        .handler = ota_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &ota_uri);

    const httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_get_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &status_uri);

    ESP_LOGI(TAG, "OTA server on port %d  POST /ota  GET /status", WST_OTA_PORT);
    return ESP_OK;
}
