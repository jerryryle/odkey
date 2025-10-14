#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "usb_core.h"
#include "usb_keyboard.h"
#include "usb_keyboard_keys.h"
#include "program_upload.h"
#include "vm_task.h"
#include "button_handler.h"
#include "program_storage.h"
#include "sdkconfig.h"

static const char *TAG = "main";

#define BUTTON_GPIO 5
#define BUTTON_DEBOUNCE_MS 50

// Comprehensive test program that exercises all VM opcodes:
// 1. Press A (KEYDN/KEYUP)
// 2. Repeat B keydn/keyup 3 times (SET_COUNTER, DEC, JNZ)
// 3. Final C keydn followed by KEYUP_ALL
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

// Button press callback
static void on_button_press(void) {
    if (!vm_task_is_running()) {
        // Load program from flash
        size_t program_size;
        const uint8_t* program = program_storage_get(&program_size);
        
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

void app_main() {
    ESP_LOGI(TAG, "Starting ODKey");

    // Initialize program storage
    if (!program_storage_init()) {
        ESP_LOGE(TAG, "Failed to initialize program storage");
        return;
    }    

    // Initialize USB core module
    if (!usb_core_init()) {
        ESP_LOGE(TAG, "Failed to initialize USB core");
        return;
    }

    // Initialize USB keyboard module (interface 0)
    if (!usb_keyboard_init(USB_KEYBOARD_INTERFACE_NUM)) {
        ESP_LOGE(TAG, "Failed to initialize USB keyboard");
        return;
    }

    // Initialize program upload module (interface 1)
    if (!program_upload_init(USB_PROGRAM_UPLOAD_INTERFACE_NUM)) {
        ESP_LOGE(TAG, "Failed to initialize program upload");
        return;
    }

    // Wait for USB device to be ready
    ESP_LOGI(TAG, "Waiting for USB device to be ready...");
    while (!usb_core_is_ready()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "USB device ready!");

    // Initialize program storage
    if (!program_storage_init()) {
        ESP_LOGE(TAG, "Failed to initialize program storage");
        return;
    }

    // Initialize VM task
    if (!vm_task_init(usb_keyboard_send_keys)) {
        ESP_LOGE(TAG, "Failed to initialize VM task");
        return;
    }

    // Initialize button handler
    if (!button_handler_init(BUTTON_GPIO, BUTTON_DEBOUNCE_MS, on_button_press)) {
        ESP_LOGE(TAG, "Failed to initialize button handler");
        return;
    }

    ESP_LOGI(TAG, "System ready! Press button on GPIO %d to run program", BUTTON_GPIO);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
