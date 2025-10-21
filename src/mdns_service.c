#include "mdns_service.h"
#include <string.h>
#include "esp_log.h"
#include "mdns.h"
#include "nvs_odkey.h"

static const char *TAG = "mdns_service";

// Configuration defaults
#define MDNS_HOSTNAME_DEFAULT "odkey"
#define MDNS_INSTANCE_DEFAULT "ODKey Device"

bool mdns_service_init(void) {
    // Initialize mDNS
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mDNS: %s", esp_err_to_name(err));
        return false;
    }

    // Load mDNS configuration from NVS
    nvs_handle_t nvs_handle;
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for mDNS config, using defaults");
        mdns_hostname_set(MDNS_HOSTNAME_DEFAULT);
        mdns_instance_name_set(MDNS_INSTANCE_DEFAULT);
        ESP_LOGI(
            TAG, "mDNS initialized with defaults: %s.local", MDNS_HOSTNAME_DEFAULT);
        return true;
    }

    // Try to get mDNS hostname
    char hostname[32];
    size_t required_size = sizeof(hostname);
    err = nvs_get_str(nvs_handle, NVS_KEY_MDNS_HOSTNAME, hostname, &required_size);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Found mDNS hostname in NVS: %s", hostname);
    } else {
        ESP_LOGI(TAG, "mDNS hostname not found in NVS, using default");
        strcpy(hostname, MDNS_HOSTNAME_DEFAULT);
    }

    // Try to get mDNS instance name
    char instance[64];
    required_size = sizeof(instance);
    err = nvs_get_str(nvs_handle, NVS_KEY_MDNS_INSTANCE, instance, &required_size);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Found mDNS instance in NVS: %s", instance);
    } else {
        ESP_LOGI(TAG, "mDNS instance not found in NVS, using default");
        strcpy(instance, MDNS_INSTANCE_DEFAULT);
    }

    nvs_close(nvs_handle);

    // Set hostname
    err = mdns_hostname_set(hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mDNS hostname: %s", esp_err_to_name(err));
        return false;
    }

    // Set instance name
    err = mdns_instance_name_set(instance);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mDNS instance name: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "mDNS service initialized: %s.local", hostname);
    return true;
}
