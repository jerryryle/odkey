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

#if 0
    // Start auto-typing the 'A' key every 1000ms
    if (!usb_keyboard_start_auto_typing(KEY_A, 1000)) {
        ESP_LOGE(TAG, "Failed to start auto-typing");
        return;
    }
#endif
    
    ESP_LOGI(TAG, "USB HID Keyboard started - pressing 'A' key every 5 seconds");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
