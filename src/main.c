#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "usb_keyboard.h"
#include "usb_keyboard_keys.h"
#include "odkeyscript_vm.h"
#include "sdkconfig.h"

static const char *TAG = "main";

#define LED_PIN 13

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
    
    // Initialize VM
    vm_context_t vm_ctx;
    if (!vm_init(&vm_ctx)) {
        ESP_LOGE(TAG, "Failed to initialize VM");
        return;
    }
    
    // Test program: press A, wait 100ms, release A, wait 100ms, press B, wait 100ms, release B
    // This corresponds to: press A; pause 100; press B; pause 100;
    uint8_t test_program[] = {
        // Press A
        0x10, 0x00, 0x01, 0x04,  // KEYDN: modifier=0, keycount=1, key=KEY_A
        0x13, 0x64, 0x00,        // WAIT: 100ms
        0x11, 0x00, 0x01, 0x04,  // KEYUP: modifier=0, keycount=1, key=KEY_A
        0x13, 0x64, 0x00,        // WAIT: 100ms
        // Press B
        0x10, 0x00, 0x01, 0x05,  // KEYDN: modifier=0, keycount=1, key=KEY_B
        0x13, 0x64, 0x00,        // WAIT: 100ms
        0x11, 0x00, 0x01, 0x05,  // KEYUP: modifier=0, keycount=1, key=KEY_B
    };
    
    ESP_LOGI(TAG, "Running test program...");
    vm_error_t result = vm_run(&vm_ctx, test_program, sizeof(test_program));
    
    if (result == VM_ERROR_NONE) {
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
