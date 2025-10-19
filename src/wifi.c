#include "wifi.h"
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_odkey.h"

static const char *TAG = "wifi";

// Configuration defaults
#define WIFI_SSID_DEFAULT ""
#define WIFI_PASSWORD_DEFAULT ""
#define WIFI_CONNECT_TIMEOUT_DEFAULT 10000

struct wifi_config_t {
    char ssid[32];
    char password[64];
    uint32_t connect_timeout_ms;
} g_wifi_config = {0};

// WiFi state
static char g_ip_address[16] = {0};
static bool g_wifi_connected = false;

// WiFi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started, attempting to connect...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "WiFi disconnected (reason: %d), attempting to reconnect...", disconnected->reason);
        g_wifi_connected = false;
        // ESP-IDF handles both initial connection failures and disconnections with this event
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(g_ip_address, sizeof(g_ip_address), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: %s", g_ip_address);
        g_wifi_connected = true;
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

    nvs_close(nvs_handle);
    return true;
}

// Initialize WiFi in station mode (without starting)
static esp_err_t wifi_init_sta(void) {
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

    ESP_LOGI(TAG, "WiFi station mode initialized for %s", g_wifi_config.ssid);
    return ESP_OK;
}

bool wifi_init(void) {
    static bool initialized = false;
    if (initialized) {
        ESP_LOGW(TAG, "WiFi module already initialized");
        return true;
    }

    // Load configuration
    if (!load_wifi_configuration(&g_wifi_config)) {
        ESP_LOGE(TAG, "Failed to load WiFi configuration");
        return false;
    }
    ESP_LOGI(TAG, "WiFi configuration loaded");

    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());

    // Initialize WiFi station mode (without starting)
    if (wifi_init_sta() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi station mode");
        return false;
    }

    ESP_LOGI(TAG, "WiFi module initialized");
    initialized = true;
    return true;
}

bool wifi_start(void) {
    static bool started = false;
    if (started) {
        ESP_LOGW(TAG, "WiFi already started");
        return true;
    }

    // Start WiFi - the event handler will automatically connect when STA starts
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi started, will attempt to connect to %s", g_wifi_config.ssid);
    started = true;
    return true;
}

bool wifi_is_connected(void) {
    return g_wifi_connected;
}

const char *wifi_get_ip_address(void) {
    return g_wifi_connected ? g_ip_address : "";
}
