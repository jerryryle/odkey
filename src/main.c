#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "usb_keyboard.h"
#include "usb_keyboard_keys.h"
#include "odkeyscript_vm.h"
#include "program_storage.h"
#include "sdkconfig.h"

static const char *TAG = "main";

#define LED_PIN 13

// Delay callback function for VM
static void delay_callback(uint16_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void app_main() {
    ESP_LOGI(TAG, "Starting USB HID Keyboard Demo");
    
    // Configure LED GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "LED configured on GPIO %d", LED_PIN);

    // Initialize USB keyboard module
    if (!usb_keyboard_init()) {
        ESP_LOGE(TAG, "Failed to initialize USB keyboard");
        return;
    }

    ESP_LOGI(TAG, "USB HID Keyboard initialized successfully");

    // Wait for USB device to be ready
    ESP_LOGI(TAG, "Waiting for USB device to be ready...");
    while (!usb_keyboard_is_ready()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Give the system a moment to settle
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "USB device ready! Testing ODKeyScript VM...");
    
    // Initialize program storage
    if (!program_storage_init()) {
        ESP_LOGE(TAG, "Failed to initialize program storage");
        return;
    }
    
    // Initialize VM
    vm_context_t vm_ctx;
    if (!vm_init(&vm_ctx)) {
        ESP_LOGE(TAG, "Failed to initialize VM");
        return;
    }
    
    // Comprehensive test program that exercises all VM opcodes:
    // 1. Press A (KEYDN/KEYUP)
    // 2. Repeat B keydn/keyup 3 times (SET_COUNTER, DEC, JNZ)
    // 3. Final C keydn followed by KEYUP_ALL
    uint8_t test_program[] = {
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
        0x13, 0x19, 0x00,        // WAIT: 25ms
        0x15, 0x00,              // DEC: counter[0]
        0x16, 0x12, 0x00, 0x00, 0x00, // JNZ: jump to address 18 if counter != 0
        
        // 3. Final C keydn followed by KEYUP_ALL
        0x10, 0x00, 0x01, 0x06,  // KEYDN: modifier=0, keycount=1, key=KEY_C
        0x13, 0x19, 0x00,        // WAIT: 25ms
        0x12,                    // KEYUP_ALL: release all keys
    };
    
    // Load default program from flash
    size_t program_size;
    const uint8_t* program = program_storage_get(&program_size);
    
    // Use program from flash if available, otherwise use built-in test program
    if (program == NULL || program_size == 0) {
        ESP_LOGI(TAG, "No program in storage, using built-in test program");
        program = test_program;
        program_size = sizeof(test_program);
    } else {
        ESP_LOGI(TAG, "Loaded program from storage (%zu bytes)", program_size);
    }
        
    ESP_LOGI(TAG, "Starting program...");
    vm_error_t result = vm_start(&vm_ctx, program, program_size, usb_keyboard_send_keys, delay_callback);
    
    if (result != VM_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to start VM: %s", vm_error_to_string(result));
        return;
    }
    
    // Step through the program
    while (vm_running(&vm_ctx)) {
        result = vm_step(&vm_ctx);
        if (result != VM_ERROR_NONE) {
            ESP_LOGE(TAG, "VM step failed: %s", vm_error_to_string(result));
            break;
        }
    }
    
    if (!vm_has_error(&vm_ctx)) {
        ESP_LOGI(TAG, "Test program completed successfully!");
        
        // Print statistics
        uint32_t instructions, keys_pressed, keys_released;
        vm_get_stats(&vm_ctx, &instructions, &keys_pressed, &keys_released);
        ESP_LOGI(TAG, "VM Stats - Instructions: %lu, Keys Pressed: %lu, Keys Released: %lu", 
                 instructions, keys_pressed, keys_released);
    } else {
        ESP_LOGE(TAG, "Test program failed: %s", vm_error_to_string(result));
    }
    
    ESP_LOGI(TAG, "Test completed. Entering main loop...");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
