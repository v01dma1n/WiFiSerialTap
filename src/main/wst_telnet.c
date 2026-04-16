// wst_telnet.c — Single-client TCP/Telnet server

#include "wst_main.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>

static const char *TAG = "wst_telnet";

static int s_server_fd = -1;
static int s_client_fd = -1;
static SemaphoreHandle_t s_lock = NULL;

// ---------------------------------------------------------------------------
// Accept task — runs in background, accepts one client at a time
// ---------------------------------------------------------------------------
static void accept_task(void *arg)
{
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int new_fd = accept(s_server_fd,
                            (struct sockaddr *)&client_addr, &addr_len);
        if (new_fd < 0) {
            ESP_LOGE(TAG, "accept failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Set non-blocking and TCP_NODELAY
        int flags = fcntl(new_fd, F_GETFL, 0);
        fcntl(new_fd, F_SETFL, flags | O_NONBLOCK);
        int nodelay = 1;
        setsockopt(new_fd, IPPROTO_TCP, TCP_NODELAY,
                   &nodelay, sizeof(nodelay));

        xSemaphoreTake(s_lock, portMAX_DELAY);

        // Kick previous client
        if (s_client_fd >= 0) {
            ESP_LOGW(TAG, "replacing previous client");
            close(s_client_fd);
        }
        s_client_fd = new_fd;

        xSemaphoreGive(s_lock);

        ESP_LOGI(TAG, "client connected from %s",
                 inet_ntoa(client_addr.sin_addr));
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
esp_err_t wst_telnet_init(uint16_t port)
{
    s_lock = xSemaphoreCreateMutex();

    s_server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_server_fd < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        return ESP_FAIL;
    }

    int reuse = 1;
    setsockopt(s_server_fd, SOL_SOCKET, SO_REUSEADDR,
               &reuse, sizeof(reuse));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(s_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed: errno %d", errno);
        close(s_server_fd);
        return ESP_FAIL;
    }

    if (listen(s_server_fd, 1) < 0) {
        ESP_LOGE(TAG, "listen() failed: errno %d", errno);
        close(s_server_fd);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "listening on port %u", port);

    xTaskCreate(accept_task, "telnet_accept", 4096, NULL, 2, NULL);

    return ESP_OK;
}

bool wst_telnet_has_client(void)
{
    return s_client_fd >= 0;
}

void wst_telnet_send(const uint8_t *data, size_t len)
{
    if (s_client_fd < 0 || len == 0) return;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_client_fd >= 0) {
        int sent = send(s_client_fd, data, len, MSG_DONTWAIT);
        if (sent < 0 && errno != EAGAIN) {
            ESP_LOGW(TAG, "client send error, disconnecting");
            close(s_client_fd);
            s_client_fd = -1;
        }
    }
    xSemaphoreGive(s_lock);
}

size_t wst_telnet_recv(uint8_t *buf, size_t max_len)
{
    if (s_client_fd < 0) return 0;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t result = 0;
    if (s_client_fd >= 0) {
        int n = recv(s_client_fd, buf, max_len, MSG_DONTWAIT);
        if (n > 0) {
            result = (size_t)n;
        } else if (n == 0) {
            // Client closed connection
            ESP_LOGI(TAG, "client disconnected");
            close(s_client_fd);
            s_client_fd = -1;
        }
        // n < 0 with EAGAIN is normal for non-blocking
    }
    xSemaphoreGive(s_lock);
    return result;
}
