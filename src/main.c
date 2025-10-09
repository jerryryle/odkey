#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "usb_keyboard.h"
#include "usb_keyboard_keys.h"
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

    ESP_LOGI(TAG, "USB device ready! Sending single 'A' key press...");
    
    // Send single 'A' key press using the API
    uint8_t keys_1[] = {KEY_LEFTSHIFT};
    usb_keyboard_send_keycodes(keys_1, sizeof(keys_1));

    // Wait a bit
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t keys_2[] = {KEY_LEFTSHIFT, KEY_A};
    usb_keyboard_send_keycodes(keys_2, sizeof(keys_2));

    // Wait a bit
    vTaskDelay(pdMS_TO_TICKS(500));
        
        // Release the key (send empty keycode array)
    usb_keyboard_send_keycodes(NULL, 0);
    
    ESP_LOGI(TAG, "Test completed. Entering main loop...");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
