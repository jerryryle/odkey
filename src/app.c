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

// Callback declarations
static void on_button_press(void);
static bool on_program_upload_start(void);

bool app_init(void) {
    // Initialize NVS ODKey module
    if (!nvs_odkey_init()) {
        ESP_LOGE(TAG, "Failed to initialize NVS ODKey module");
        return false;
    }

    // Initialize event loop (moved from wifi.c)
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize program storage
    if (!program_init()) {
        ESP_LOGE(TAG, "Failed to initialize program storage");
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
    if (!usb_system_config_init(USB_SYSTEM_CONFIG_INTERFACE_NUM,
                                on_program_upload_start)) {
        ESP_LOGE(TAG, "Failed to initialize USB system config");
        return false;
    }

    // Initialize VM task
    if (!vm_task_init(usb_keyboard_send_keys)) {
        ESP_LOGE(TAG, "Failed to initialize VM task");
        return false;
    }

    // Initialize button
    if (!button_init(BUTTON_GPIO, BUTTON_DEBOUNCE_MS, on_button_press)) {
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
        if (!http_service_init(on_program_upload_start)) {
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

// Button press callback implementation
static void on_button_press(void) {
    if (!vm_task_is_running()) {
        // Load program from flash
        uint32_t program_size;
        const uint8_t *program = program_get(PROGRAM_TYPE_FLASH, &program_size);

        if (program == NULL || program_size == 0) {
            ESP_LOGI(TAG, "No valid program in storage. Ignoring button press.");
            return;
        }
        ESP_LOGI(TAG,
                 "Loaded program from storage (%lu bytes)",
                 (unsigned long)program_size);

        // Start program execution
        if (vm_task_start_program(program, program_size)) {
            ESP_LOGI(TAG, "Program execution started");
        } else {
            ESP_LOGW(TAG, "Failed to start program execution");
        }
    } else {
        ESP_LOGD(TAG, "Button pressed but program already running, ignoring");
    }
}

// Program upload start callback implementation
static bool on_program_upload_start(void) {
    return vm_task_halt();
}
