#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "usb_keyboard.h"
#include "usb_keyboard_keys.h"
#include "sdkconfig.h"

static const char *TAG = "main";

void app_main() {
    ESP_LOGI(TAG, "Starting USB HID Keyboard Demo");
    
    // Initialize USB keyboard module
    if (!usb_keyboard_init()) {
        ESP_LOGE(TAG, "Failed to initialize USB keyboard");
        return;
    }
    
    // Start auto-typing the 'A' key every 1000ms
    if (!usb_keyboard_start_auto_typing(KEY_A, 1000)) {
        ESP_LOGE(TAG, "Failed to start auto-typing");
        return;
    }
    
    ESP_LOGI(TAG, "USB HID Keyboard started - pressing 'A' key every second");
    
    // Main loop - just keep the task alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}