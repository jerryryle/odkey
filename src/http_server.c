#include "http_server.h"
#include <string.h>
#include <sys/time.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_config.h"
#include "wifi.h"

static const char *TAG = "http_server";

// Configuration defaults
#define HTTP_SERVER_PORT_DEFAULT 80

// HTTP Server Configuration
#define HTTP_SERVER_MAX_URI_HANDLERS 8
#define HTTP_SERVER_MAX_RESP_HEADERS 8
#define HTTP_SERVER_MAX_OPEN_SOCKETS 7

struct http_server_config_t {
    uint16_t server_port;
} g_http_server_config = {0};

// HTTP server handle
static httpd_handle_t g_server = NULL;

// Get HTTP server configuration from NVS or use defaults
static bool load_http_server_configuration(struct http_server_config_t *config) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    memset(config, 0, sizeof(*config));

    // Try to open NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS");
        return false;
    }

    // Try to get server port
    err = nvs_get_u16(nvs_handle, NVS_KEY_HTTP_SERVER_PORT, &config->server_port);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Found server port in NVS: %d", config->server_port);
    } else {
        ESP_LOGI(TAG, "Server port not found in NVS, using default");
        config->server_port = HTTP_SERVER_PORT_DEFAULT;
    }

    nvs_close(nvs_handle);
    return true;
}

// HTTP GET /api/status handler
static esp_err_t status_handler(httpd_req_t *req) {
    // Get uptime
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t uptime_ms = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    // Get IP address from WiFi module
    const char *ip_address = wifi_get_ip_address();

    // Build JSON response
    char response[256];
    snprintf(response, sizeof(response),
             "{"
             "\"wifi_mode\":\"sta\","
             "\"ip_address\":\"%s\","
             "\"uptime_ms\":%llu"
             "}",
             ip_address,
             uptime_ms);

    // Set content type and send response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

bool http_server_init(void) {
    // Load configuration
    if (!load_http_server_configuration(&g_http_server_config)) {
        ESP_LOGE(TAG, "Failed to load HTTP server configuration");
        return false;
    }
    ESP_LOGI(TAG, "HTTP server configuration loaded");
    return true;
}

uint16_t http_server_get_port(void) {
    return g_http_server_config.server_port;
}

esp_err_t http_server_start(void) {
    if (g_server != NULL) {
        ESP_LOGW(TAG, "HTTP server already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = g_http_server_config.server_port;
    config.max_uri_handlers = HTTP_SERVER_MAX_URI_HANDLERS;
    config.max_resp_headers = HTTP_SERVER_MAX_RESP_HEADERS;
    config.max_open_sockets = HTTP_SERVER_MAX_OPEN_SOCKETS;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);
    esp_err_t err = httpd_start(&g_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        g_server = NULL;
        return err;
    }
    // Register URI handlers
    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(g_server, &status_uri);

    ESP_LOGI(TAG, "HTTP server started successfully");
    return ESP_OK;
}

esp_err_t http_server_stop(void) {
    if (g_server == NULL) {
        ESP_LOGW(TAG, "HTTP server is not running");
        return ESP_OK;
    }
    esp_err_t err = httpd_stop(g_server);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop HTTP server: %s", esp_err_to_name(err));
    }
    g_server = NULL;
    ESP_LOGI(TAG, "HTTP server stopped");
    return err;
}

bool http_server_is_running(void) {
    return (g_server != NULL);
}
