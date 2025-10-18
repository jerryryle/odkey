#include "freertos/task.h"
#include "http_api.h"
#include <string.h>
#include <sys/time.h>
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_config.h"
#include "nvs_flash.h"

static const char *TAG = "http_api";

// Configuration defaults
#define WIFI_SSID_DEFAULT ""
#define WIFI_PASSWORD_DEFAULT ""
#define WIFI_CONNECT_TIMEOUT_DEFAULT 10000
#define HTTP_SERVER_PORT_DEFAULT 80

// HTTP Server Configuration
#define HTTP_SERVER_MAX_URI_LEN 512
#define HTTP_SERVER_MAX_REQ_HDR_LEN 512

// WiFi event group bits
#define WIFI_STA_READY_BIT BIT0
#define WIFI_CONNECTED_BIT BIT1
#define WIFI_DISCONNECTED_BIT BIT2

struct http_api_config_t {
    char ssid[32];
    char password[64];
    uint32_t connect_timeout_ms;
    uint16_t server_port;
} g_http_api_config = {0};

// HTTP server handle
static httpd_handle_t g_server = NULL;

// WiFi state
static EventGroupHandle_t g_wifi_event_group = NULL;
static char g_ip_address[16] = {0};

// Task handle
static TaskHandle_t s_wifi_task_handle = NULL;

// WiFi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started");
        xEventGroupSetBits(g_wifi_event_group, WIFI_STA_READY_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected");
        xEventGroupSetBits(g_wifi_event_group, WIFI_DISCONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(g_ip_address, sizeof(g_ip_address), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: %s", g_ip_address);
        xEventGroupSetBits(g_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// HTTP GET /api/status handler
static esp_err_t status_handler(httpd_req_t *req) {
    // Get uptime
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t uptime_ms = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    // Build JSON response
    char response[256];
    snprintf(response, sizeof(response),
             "{"
             "\"wifi_mode\":\"sta\","
             "\"ip_address\":\"%s\","
             "\"uptime_ms\":%llu"
             "}",
             g_ip_address,
             uptime_ms);

    // Set content type and send response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

// Start HTTP server
static esp_err_t start_http_server(void) {
    if (g_server != NULL) {
        ESP_LOGW(TAG, "HTTP server already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = g_http_api_config.server_port;
    config.max_uri_handlers = 8;
    config.max_resp_headers = 8;
    config.max_open_sockets = 7;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);
    if (httpd_start(&g_server, &config) == ESP_OK) {
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

    ESP_LOGE(TAG, "Error starting HTTP server");
    return ESP_FAIL;
}

// Get WiFi credentials from NVS or use defaults
static bool load_configuration(struct http_api_config_t *config) {
    nvs_handle_t nvs_handle;
    esp_err_t err;
    size_t required_size;

    memset(config, 0, sizeof(*config));

    // Try to open NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS");
        return false;
    }

    // Try to get SSID
    required_size = sizeof(config->ssid);
    err = nvs_get_str(nvs_handle, NVS_KEY_WIFI_SSID, g_http_api_config.ssid, &required_size);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Found SSID in NVS: %s", config->ssid);
    } else {
        ESP_LOGI(TAG, "SSID not found in NVS, using default");
        strcpy(config->ssid, WIFI_SSID_DEFAULT);
    }

    // Try to get password
    required_size = sizeof(config->password);
    err = nvs_get_str(nvs_handle, NVS_KEY_WIFI_PASSWORD, config->password, &required_size);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Found password in NVS");
    } else {
        ESP_LOGI(TAG, "Password not found in NVS, using default");
        strcpy(config->password, WIFI_PASSWORD_DEFAULT);
    }

    // Try to get connect timeout
    err = nvs_get_u32(nvs_handle, NVS_KEY_WIFI_CONNECT_TIMEOUT, &config->connect_timeout_ms);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Found connect timeout in NVS: %lu", config->connect_timeout_ms);
    } else {
        ESP_LOGI(TAG, "Connect timeout not found in NVS, using default");
        config->connect_timeout_ms = WIFI_CONNECT_TIMEOUT_DEFAULT;
    }

    // Try to get server port
    required_size = sizeof(config->server_port);
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

// Initialize WiFi in station mode
static esp_err_t wifi_init_sta(void) {
    g_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false},
        },
    };

    // Copy credentials to wifi_config
    strncpy((char *)wifi_config.sta.ssid, g_http_api_config.ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, g_http_api_config.password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi station mode initialized, attempting to connect to %s", g_http_api_config.ssid);
    return ESP_OK;
}

// WiFi management task
static void wifi_task(void *pvParameters) {
    bool wifi_connected = false;
    // Initialize WiFi station mode
    if (wifi_init_sta() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi station mode");
        s_wifi_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Wait for WiFi station to be ready
    ESP_LOGI(TAG, "Waiting for WiFi station to be ready...");
    EventBits_t bits = xEventGroupWaitBits(g_wifi_event_group,
                                           WIFI_STA_READY_BIT,
                                           pdTRUE,  // Clear the bit when we get it
                                           pdFALSE,
                                           portMAX_DELAY);  // Wait indefinitely for STA ready

    if (!(bits & WIFI_STA_READY_BIT)) {
        ESP_LOGE(TAG, "Failed to wait for WiFi station ready");
        s_wifi_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "WiFi station ready, starting connection management");

    // Main WiFi management loop
    for (;;) {
        if (wifi_connected) {
            // WiFi is connected - wait for disconnection event
            EventBits_t bits = xEventGroupWaitBits(g_wifi_event_group,
                                                   WIFI_DISCONNECTED_BIT,
                                                   pdTRUE,  // Clear the bit when we get it
                                                   pdFALSE,
                                                   portMAX_DELAY);  // Wait indefinitely for disconnection

            if (bits & WIFI_DISCONNECTED_BIT) {
                ESP_LOGW(TAG, "WiFi disconnected, stopping HTTP server...");
                wifi_connected = false;

                // Stop HTTP server if running
                if (g_server != NULL) {
                    httpd_stop(g_server);
                    g_server = NULL;
                    ESP_LOGI(TAG, "HTTP server stopped due to WiFi disconnection");
                }
            }
        } else {
            // WiFi is disconnected - try to connect with timeout
            ESP_LOGI(TAG, "Attempting to connect to WiFi...");
            esp_wifi_connect();

            // Wait for connection with timeout
            EventBits_t bits = xEventGroupWaitBits(g_wifi_event_group,
                                                   WIFI_CONNECTED_BIT,
                                                   pdTRUE,  // Clear the bit when we get it
                                                   pdFALSE,
                                                   pdMS_TO_TICKS(g_http_api_config.connect_timeout_ms));

            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGI(TAG, "Connected to WiFi successfully");
                wifi_connected = true;

                // Start HTTP server if not already running
                if (g_server == NULL) {
                    if (start_http_server() == ESP_OK) {
                        ESP_LOGI(TAG, "HTTP API module ready");
                    } else {
                        ESP_LOGE(TAG, "Failed to start HTTP server");
                    }
                }
            } else {
                // Connection timeout - will retry in next loop iteration
                ESP_LOGW(TAG, "WiFi connection timeout, will retry...");
            }
        }
    }
}

bool http_api_init(void) {
    if (s_wifi_task_handle != NULL) {
        ESP_LOGW(TAG, "HTTP API module already initialized");
        return true;
    }

    // Load configuration
    if (!load_configuration(&g_http_api_config)) {
        ESP_LOGE(TAG, "Failed to load HTTP API configuration");
        return false;
    }
    ESP_LOGI(TAG, "HTTP API configuration loaded");

    // Create WiFi management task
    BaseType_t ret = xTaskCreate(wifi_task, "wifi_task", 4096, NULL, 5, &s_wifi_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WiFi task");
        return false;
    }

    ESP_LOGI(TAG, "HTTP API module initialization started");
    return true;
}

bool http_api_is_ready(void) {
    return (g_server != NULL);
}
