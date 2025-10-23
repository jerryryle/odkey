#include "app.h"
#include "button.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_service.h"
#include "mdns_service.h"
#include "nvs_odkey.h"
#include "program.h"
#include "usb_core.h"
#include "usb_keyboard.h"
#include "usb_system_config.h"
#include "vm_task.h"
#include "wifi.h"

static const char *TAG = "app";

#define BUTTON_GPIO 5
#define BUTTON_DEBOUNCE_MS 50
#define BUTTON_REPEAT_DELAY_MS 225

bool app_init(void) {
    // Initialize NVS ODKey module
    if (!nvs_odkey_init()) {
        ESP_LOGE(TAG, "Failed to initialize NVS ODKey module");
        return false;
    }

    // Initialize event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize program
    if (!program_init(usb_keyboard_send_keys)) {
        ESP_LOGE(TAG, "Failed to initialize program");
        return false;
    }

    // Initialize USB core module
    if (!usb_core_init()) {
        ESP_LOGE(TAG, "Failed to initialize USB core");
        return false;
    }

    // Initialize USB keyboard module
    if (!usb_keyboard_init(USB_KEYBOARD_INTERFACE_NUM)) {
        ESP_LOGE(TAG, "Failed to initialize USB keyboard");
        return false;
    }

    // Initialize USB system config module
    if (!usb_system_config_init(USB_SYSTEM_CONFIG_INTERFACE_NUM)) {
        ESP_LOGE(TAG, "Failed to initialize USB system config");
        return false;
    }

    // Initialize button
    if (!button_init(BUTTON_GPIO, BUTTON_DEBOUNCE_MS, BUTTON_REPEAT_DELAY_MS)) {
        ESP_LOGE(TAG, "Failed to initialize button");
        return false;
    }

    // Initialize WiFi
    if (!wifi_init()) {
        ESP_LOGE(TAG, "Failed to initialize WiFi. Skipping other network services.");
    } else {
        // Initialize mDNS service
        if (!mdns_service_init()) {
            ESP_LOGW(TAG, "Failed to initialize mDNS service, continuing without it");
        }

        // Initialize HTTP service module
        if (!http_service_init()) {
            ESP_LOGE(TAG, "Failed to initialize HTTP service");
            return false;
        }

        // Start WiFi connection
        if (!wifi_start()) {
            ESP_LOGE(TAG, "Failed to start WiFi");
            return false;
        }
    }

    ESP_LOGI(TAG, "System ready! Press button on GPIO %d to run program", BUTTON_GPIO);
    return true;
}
