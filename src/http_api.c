#include "http_api.h"
#include "wifi_config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include <string.h>
#include <sys/time.h>

static const char *TAG = "http_api";

// WiFi event group bits
#define WIFI_CONNECTED_BIT BIT0

// HTTP server handle
static httpd_handle_t s_server = NULL;

// WiFi state
static EventGroupHandle_t s_wifi_event_group = NULL;
static bool s_wifi_connected = false;
static char s_ip_address[16] = {0};

// Task handle
static TaskHandle_t s_wifi_task_handle = NULL;

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, will retry in %d seconds", WIFI_RETRY_INTERVAL_MS / 1000);
        s_wifi_connected = false;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        snprintf(s_ip_address, sizeof(s_ip_address), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: %s", s_ip_address);
        s_wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// HTTP GET /api/status handler
static esp_err_t status_handler(httpd_req_t *req)
{
    // Get uptime
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t uptime_ms = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    // Build JSON response
    char response[256];
    snprintf(response, sizeof(response),
        "{"
        "\"wifi_mode\":\"sta\","
        "\"wifi_connected\":%s,"
        "\"ip_address\":\"%s\","
        "\"uptime_ms\":%llu"
        "}",
        s_wifi_connected ? "true" : "false",
        s_ip_address,
        uptime_ms
    );

    // Set content type and send response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    
    return ESP_OK;
}


// Start HTTP server
static esp_err_t start_http_server(void)
{
    if (s_server != NULL) {
        ESP_LOGW(TAG, "HTTP server already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = HTTP_SERVER_PORT;
    config.max_uri_handlers = 8;
    config.max_resp_headers = 8;
    config.max_open_sockets = 7;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);
    if (httpd_start(&s_server, &config) == ESP_OK) {
        // Register URI handlers
        httpd_uri_t status_uri = {
            .uri       = "/api/status",
            .method    = HTTP_GET,
            .handler   = status_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(s_server, &status_uri);
        
        ESP_LOGI(TAG, "HTTP server started successfully");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Error starting HTTP server");
    return ESP_FAIL;
}


// Get WiFi credentials from NVS or use defaults
static void get_wifi_credentials(char* ssid, char* password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    size_t required_size;

    // Initialize with defaults
    strcpy(ssid, WIFI_SSID_DEFAULT);
    strcpy(password, WIFI_PASSWORD_DEFAULT);

    // Try to open NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "NVS open failed, using default credentials");
        return;
    }

    // Try to get SSID
    required_size = 32;
    err = nvs_get_str(nvs_handle, NVS_KEY_SSID, ssid, &required_size);
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "Found SSID in NVS: %s", ssid);
    } else {
        ESP_LOGD(TAG, "SSID not found in NVS, using default");
        strcpy(ssid, WIFI_SSID_DEFAULT);
    }

    // Try to get password
    required_size = 64;
    err = nvs_get_str(nvs_handle, NVS_KEY_PASSWORD, password, &required_size);
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "Found password in NVS");
    } else {
        ESP_LOGD(TAG, "Password not found in NVS, using default");
        strcpy(password, WIFI_PASSWORD_DEFAULT);
    }

    nvs_close(nvs_handle);
}

// Initialize WiFi in station mode
static esp_err_t wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

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

    // Get WiFi credentials from NVS or use defaults
    char ssid[32];
    char password[64];
    get_wifi_credentials(ssid, password);

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    // Copy credentials to wifi_config
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi station mode initialized, attempting to connect to %s", ssid);
    return ESP_OK;
}


// WiFi management task
static void wifi_task(void *pvParameters)
{
    // Initialize WiFi station mode
    if (wifi_init_sta() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi station mode");
        s_wifi_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Main WiFi management loop
    while (1) {
        // Wait for connection
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                              WIFI_CONNECTED_BIT,
                                              pdTRUE,  // Clear the bit when we get it
                                              pdFALSE,
                                              pdMS_TO_TICKS(WIFI_RETRY_INTERVAL_MS));

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Connected to WiFi successfully");
            
            // Start HTTP server if not already running
            if (s_server == NULL) {
                if (start_http_server() == ESP_OK) {
                    ESP_LOGI(TAG, "HTTP API module ready");
                } else {
                    ESP_LOGE(TAG, "Failed to start HTTP server");
                }
            }
        } else {
            // Timeout - connection failed, will retry automatically
            ESP_LOGW(TAG, "WiFi connection timeout, retrying...");
            
            // Stop HTTP server if running
            if (s_server != NULL) {
                httpd_stop(s_server);
                s_server = NULL;
                ESP_LOGI(TAG, "HTTP server stopped due to WiFi disconnection");
            }
            
            // Trigger reconnection
            esp_wifi_connect();
        }
    }
}

bool http_api_init(void)
{
    if (s_wifi_task_handle != NULL) {
        ESP_LOGW(TAG, "HTTP API module already initialized");
        return true;
    }

    // Create WiFi management task
    BaseType_t ret = xTaskCreate(wifi_task, "wifi_task", 4096, NULL, 5, &s_wifi_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WiFi task");
        return false;
    }

    ESP_LOGI(TAG, "HTTP API module initialization started");
    return true;
}

bool http_api_is_ready(void)
{
    return (s_wifi_connected && s_server != NULL);
}

