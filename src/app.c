#include "app.h"
#include "button_handler.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_service.h"
#include "mdns_service.h"
#include "nvs_odkey.h"
#include "program_storage.h"
#include "usb_core.h"
#include "usb_keyboard.h"
#include "usb_system_config.h"
#include "vm_task.h"
#include "wifi.h"

static const char *TAG = "app";

#define BUTTON_GPIO 5
#define BUTTON_DEBOUNCE_MS 50

// Comprehensive test program that exercises all VM opcodes:
// 1. Press A (KEYDN/KEYUP)
// 2. Repeat B keydn/keyup 3 times (SET_COUNTER, DEC, JNZ)
// 3. Final C keydn followed by KEYUP_ALL
// clang-format off
static const uint8_t test_program[] = {
    // 1. Press A
    0x10, 0x00, 0x01, 0x04,  // KEYDN: modifier=0, keycount=1, key=KEY_A
    0x13, 0x19, 0x00,        // WAIT: 25ms
    0x11, 0x00, 0x01, 0x04,  // KEYUP: modifier=0, keycount=1, key=KEY_A
    0x13, 0x19, 0x00,        // WAIT: 25ms
    
    // 2. Set up loop counter for 3 iterations
    0x14, 0x00, 0x03, 0x00,  // SET_COUNTER: counter[0] = 3
    
    // Loop start (address 18):
    0x10, 0x00, 0x01, 0x05,  // KEYDN: modifier=0, keycount=1, key=KEY_B
    0x13, 0x19, 0x00,        // WAIT: 25ms
    0x11, 0x00, 0x01, 0x05,  // KEYUP: modifier=0, keycount=1, key=KEY_B
    0x13, 0x64, 0x00,        // WAIT: 100ms
    0x15, 0x00,              // DEC: counter[0]
    0x16, 0x12, 0x00, 0x00, 0x00, // JNZ: jump to address 18 if counter != 0
    
    // 3. Final C keydn followed by KEYUP_ALL
    0x10, 0x00, 0x01, 0x06,  // KEYDN: modifier=0, keycount=1, key=KEY_C
    0x13, 0x19, 0x00,        // WAIT: 25ms
    0x12,                    // KEYUP_ALL: release all keys
};
// clang-format on

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
    if (!program_storage_init()) {
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
    if (!usb_system_config_init(USB_SYSTEM_CONFIG_INTERFACE_NUM, on_program_upload_start)) {
        ESP_LOGE(TAG, "Failed to initialize USB system config");
        return false;
    }

    // Initialize VM task
    if (!vm_task_init(usb_keyboard_send_keys)) {
        ESP_LOGE(TAG, "Failed to initialize VM task");
        return false;
    }

    // Initialize button handler
    if (!button_handler_init(BUTTON_GPIO, BUTTON_DEBOUNCE_MS, on_button_press)) {
        ESP_LOGE(TAG, "Failed to initialize button handler");
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
        const uint8_t *program = program_storage_get(&program_size);

        // Fallback to test program if no program in flash
        if (program == NULL || program_size == 0) {
            ESP_LOGI(TAG, "No program in storage, using built-in test program");
            program = test_program;
            program_size = sizeof(test_program);
        } else {
            ESP_LOGI(TAG, "Loaded program from storage (%lu bytes)", (unsigned long)program_size);
        }

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
