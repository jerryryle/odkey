#include "http_service.h"
#include <string.h>
#include <sys/time.h>
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs_odkey.h"
#include "wifi.h"

static const char *TAG = "http_service";

// Configuration defaults
#define HTTP_SERVICE_PORT_DEFAULT 80

// HTTP Service Configuration
#define HTTP_SERVICE_MAX_URI_HANDLERS 8
#define HTTP_SERVICE_MAX_RESP_HEADERS 8
#define HTTP_SERVICE_MAX_OPEN_SOCKETS 7

struct http_service_config_t {
    uint16_t service_port;
} g_http_service_config = {0};

// HTTP service handle
static httpd_handle_t g_service = NULL;

// Event handler instances
static esp_event_handler_instance_t g_wifi_event_instance = NULL;
static esp_event_handler_instance_t g_ip_event_instance = NULL;

// Forward declarations
static esp_err_t start_http_service(void);
static void stop_http_service(void);

// Get HTTP service configuration from NVS or use defaults
static bool load_http_service_configuration(struct http_service_config_t *config) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    memset(config, 0, sizeof(*config));

    // Try to open NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS");
        return false;
    }

    // Try to get service port
    err = nvs_get_u16(nvs_handle, NVS_KEY_HTTP_SERVER_PORT, &config->service_port);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Found service port in NVS: %d", config->service_port);
    } else {
        ESP_LOGI(TAG, "Service port not found in NVS, using default");
        config->service_port = HTTP_SERVICE_PORT_DEFAULT;
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

// WiFi event handler for HTTP service
static void http_service_wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (g_service != NULL) {
            ESP_LOGI(TAG, "WiFi disconnected, stopping HTTP service");
            stop_http_service();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "WiFi connected, starting HTTP service");
        start_http_service();
    }
}

bool http_service_init(void) {
    // Load configuration
    if (!load_http_service_configuration(&g_http_service_config)) {
        ESP_LOGE(TAG, "Failed to load HTTP service configuration");
        return false;
    }
    ESP_LOGI(TAG, "HTTP service configuration loaded");

    // Register event handlers
    esp_err_t ret = esp_event_handler_instance_register(WIFI_EVENT,
                                                        WIFI_EVENT_STA_DISCONNECTED,
                                                        &http_service_wifi_event_handler,
                                                        NULL,
                                                        &g_wifi_event_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              &http_service_wifi_event_handler,
                                              NULL,
                                              &g_ip_event_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ret));
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, g_wifi_event_instance);
        return false;
    }

    ESP_LOGI(TAG, "HTTP service event handlers registered");
    return true;
}

uint16_t http_service_get_port(void) {
    return g_http_service_config.service_port;
}

static esp_err_t start_http_service(void) {
    if (g_service != NULL) {
        ESP_LOGW(TAG, "HTTP service already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = g_http_service_config.service_port;
    config.max_uri_handlers = HTTP_SERVICE_MAX_URI_HANDLERS;
    config.max_resp_headers = HTTP_SERVICE_MAX_RESP_HEADERS;
    config.max_open_sockets = HTTP_SERVICE_MAX_OPEN_SOCKETS;

    // Start the httpd service
    ESP_LOGI(TAG, "Starting HTTP service on port %d", config.server_port);
    esp_err_t err = httpd_start(&g_service, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP service: %s", esp_err_to_name(err));
        g_service = NULL;
        return err;
    }
    // Register URI handlers
    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(g_service, &status_uri);

    // Add HTTP service to mDNS
    esp_err_t mdns_err = mdns_service_add(NULL, "_http", "_tcp", g_http_service_config.service_port, NULL, 0);
    if (mdns_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add mDNS HTTP service: %s", esp_err_to_name(mdns_err));
    }

    ESP_LOGI(TAG, "HTTP service started successfully");
    return ESP_OK;
}

static void stop_http_service(void) {
    if (g_service == NULL) {
        ESP_LOGW(TAG, "HTTP service is not running");
        return;
    }

    // Remove HTTP service from mDNS
    esp_err_t mdns_err = mdns_service_remove("_http", "_tcp");
    if (mdns_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to remove mDNS HTTP service: %s", esp_err_to_name(mdns_err));
    }

    esp_err_t err = httpd_stop(g_service);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop HTTP service: %s", esp_err_to_name(err));
    }
    g_service = NULL;
    ESP_LOGI(TAG, "HTTP service stopped");
}

bool http_service_is_running(void) {
    return (g_service != NULL);
}
