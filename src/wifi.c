#include "wifi.h"
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "http_server.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_config.h"

static const char *TAG = "wifi";

// Configuration defaults
#define WIFI_SSID_DEFAULT ""
#define WIFI_PASSWORD_DEFAULT ""
#define WIFI_CONNECT_TIMEOUT_DEFAULT 10000
#define MDNS_HOSTNAME_DEFAULT "odkey"
#define MDNS_INSTANCE_DEFAULT "ODKey Device"

// WiFi event group bits
#define WIFI_STA_READY_BIT BIT0
#define WIFI_CONNECTED_BIT BIT1
#define WIFI_DISCONNECTED_BIT BIT2

struct wifi_config_t {
    char ssid[32];
    char password[64];
    uint32_t connect_timeout_ms;
    char mdns_hostname[32];
    char mdns_instance[64];
} g_wifi_config = {0};

// WiFi state
static EventGroupHandle_t g_wifi_event_group = NULL;
static char g_ip_address[16] = {0};
static bool g_wifi_connected = false;

// Task handle
static TaskHandle_t g_wifi_task_handle = NULL;

// WiFi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started");
        xEventGroupSetBits(g_wifi_event_group, WIFI_STA_READY_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected");
        g_wifi_connected = false;
        xEventGroupSetBits(g_wifi_event_group, WIFI_DISCONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(g_ip_address, sizeof(g_ip_address), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: %s", g_ip_address);
        g_wifi_connected = true;
        xEventGroupSetBits(g_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Get WiFi credentials from NVS or use defaults
static bool load_wifi_configuration(struct wifi_config_t *config) {
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
    err = nvs_get_str(nvs_handle, NVS_KEY_WIFI_SSID, config->ssid, &required_size);
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

    // Try to get mDNS hostname
    required_size = sizeof(config->mdns_hostname);
    err = nvs_get_str(nvs_handle, NVS_KEY_MDNS_HOSTNAME, config->mdns_hostname, &required_size);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Found mDNS hostname in NVS: %s", config->mdns_hostname);
    } else {
        ESP_LOGI(TAG, "mDNS hostname not found in NVS, using default");
        strcpy(config->mdns_hostname, MDNS_HOSTNAME_DEFAULT);
    }

    // Try to get mDNS instance name
    required_size = sizeof(config->mdns_instance);
    err = nvs_get_str(nvs_handle, NVS_KEY_MDNS_INSTANCE, config->mdns_instance, &required_size);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Found mDNS instance in NVS: %s", config->mdns_instance);
    } else {
        ESP_LOGI(TAG, "mDNS instance not found in NVS, using default");
        strcpy(config->mdns_instance, MDNS_INSTANCE_DEFAULT);
    }

    nvs_close(nvs_handle);
    return true;
}

// Initialize mDNS service
static esp_err_t init_mdns(void) {
    // Initialize mDNS
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mDNS: %s", esp_err_to_name(err));
        return err;
    }

    // Set hostname
    err = mdns_hostname_set(g_wifi_config.mdns_hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mDNS hostname: %s", esp_err_to_name(err));
        return err;
    }

    // Set instance name
    err = mdns_instance_name_set(g_wifi_config.mdns_instance);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mDNS instance name: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "mDNS initialized: %s.local", g_wifi_config.mdns_hostname);
    return ESP_OK;
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
    strncpy((char *)wifi_config.sta.ssid, g_wifi_config.ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, g_wifi_config.password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi station mode initialized, attempting to connect to %s", g_wifi_config.ssid);
    return ESP_OK;
}

// WiFi management task
static void wifi_task(void *pvParameters) {
    // Initialize WiFi station mode
    if (wifi_init_sta() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi station mode");
        g_wifi_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }
    if (init_mdns() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize mDNS, continuing without it");
    }

    // Initialize HTTP server module
    if (!http_server_init()) {
        ESP_LOGE(TAG, "Failed to initialize HTTP server module");
        g_wifi_task_handle = NULL;
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
        g_wifi_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "WiFi station ready, starting connection management");

    // Main WiFi management loop
    for (;;) {
        if (g_wifi_connected) {
            // WiFi is connected - wait for disconnection event
            EventBits_t bits = xEventGroupWaitBits(g_wifi_event_group,
                                                   WIFI_DISCONNECTED_BIT,
                                                   pdTRUE,  // Clear the bit when we get it
                                                   pdFALSE,
                                                   portMAX_DELAY);  // Wait indefinitely for disconnection

            if (bits & WIFI_DISCONNECTED_BIT) {
                ESP_LOGW(TAG, "WiFi disconnected, stopping HTTP server...");
                // Remove HTTP service from mDNS
                if (mdns_service_remove("_http", "_tcp") != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to remove mDNS HTTP service");
                }

                http_server_stop();
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
                                                   pdMS_TO_TICKS(g_wifi_config.connect_timeout_ms));

            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGI(TAG, "Connected to WiFi successfully, starting HTTP server...");
                if (http_server_start() != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to start HTTP server");
                } else {
                    // Add HTTP service to mDNS
                    esp_err_t err = mdns_service_add(NULL, "_http", "_tcp", http_server_get_port(), NULL, 0);
                    if (err != ESP_OK) {
                        ESP_LOGW(TAG, "Failed to add mDNS HTTP service: %s", esp_err_to_name(err));
                    }
                    ESP_LOGI(TAG, "HTTP server started successfully");
                }
            } else {
                // Connection timeout - will retry in next loop iteration
                ESP_LOGW(TAG, "WiFi connection timeout, will retry...");
            }
        }
    }
}

bool wifi_init(void) {
    if (g_wifi_task_handle != NULL) {
        ESP_LOGW(TAG, "WiFi module already initialized");
        return true;
    }

    // Load configuration
    if (!load_wifi_configuration(&g_wifi_config)) {
        ESP_LOGE(TAG, "Failed to load WiFi configuration");
        return false;
    }
    ESP_LOGI(TAG, "WiFi configuration loaded");

    // Create WiFi management task
    BaseType_t ret = xTaskCreate(wifi_task, "wifi_task", 4096, NULL, 5, &g_wifi_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WiFi task");
        return false;
    }

    ESP_LOGI(TAG, "WiFi module initialization started");
    return true;
}

bool wifi_is_connected(void) {
    return g_wifi_connected;
}

const char *wifi_get_ip_address(void) {
    return g_wifi_connected ? g_ip_address : "";
}

bool wifi_is_http_ready(void) {
    return http_server_is_running();
}
