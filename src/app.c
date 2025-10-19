#include "app.h"
#include "button_handler.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_config.h"
#include "nvs_flash.h"
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

// Initialize NVS and create default namespace
static bool init_nvs(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated and needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create default namespace if it doesn't exist
    nvs_handle_t nvs_handle;
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", NVS_NAMESPACE, esp_err_to_name(ret));
        return false;
    }

    // Commit to ensure namespace is created
    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS namespace: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return false;
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "NVS initialized and namespace '%s' created", NVS_NAMESPACE);
    return true;
}

bool app_init(void) {
    // Initialize NVS and create default namespace
    if (!init_nvs()) {
        ESP_LOGE(TAG, "Failed to initialize NVS");
        return false;
    }

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

    // Initialize USB keyboard module (interface 0)
    if (!usb_keyboard_init(USB_KEYBOARD_INTERFACE_NUM)) {
        ESP_LOGE(TAG, "Failed to initialize USB keyboard");
        return false;
    }

    // Initialize USB system config module (interface 1)
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

    // Initialize WiFi module (which manages HTTP server internally)
    if (!wifi_init()) {
        ESP_LOGE(TAG, "Failed to initialize WiFi module");
        return false;
    }

    ESP_LOGI(TAG, "System ready! Press button on GPIO %d to run program", BUTTON_GPIO);
    return true;
}

// Button press callback implementation
static void on_button_press(void) {
    if (!vm_task_is_running()) {
        // Load program from flash
        size_t program_size;
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
