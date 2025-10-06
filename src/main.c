#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tusb.h"
#include "tinyusb_default_config.h"
#include "sdkconfig.h"

static const char *TAG = "usb_keyboard";

// Key code for 'A' key
#define KEY_A 0x04

// Global variables
static bool key_pressed = false;
static TimerHandle_t key_timer = NULL;

// Timer callback function
static void key_timer_callback(TimerHandle_t xTimer) {
    if (tud_hid_ready()) {
        if (!key_pressed) {
            // Press the 'A' key
            uint8_t keycode[6] = {KEY_A, 0, 0, 0, 0, 0};
            tud_hid_keyboard_report(0, 0, keycode);
            ESP_LOGI(TAG, "Key A pressed");
            key_pressed = true;
        } else {
            // Release the 'A' key
            uint8_t keycode[6] = {0, 0, 0, 0, 0, 0};
            tud_hid_keyboard_report(0, 0, keycode);
            ESP_LOGI(TAG, "Key A released");
            key_pressed = false;
        }
    }
}

// TinyUSB HID callbacks
uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance) {
    // HID Report Descriptor for keyboard
    static uint8_t const desc_hid_report[] = {
        TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1))
    };
    return desc_hid_report;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;
}

void app_main() {
    ESP_LOGI(TAG, "Starting USB HID Keyboard Demo");
    
    // Initialize TinyUSB with default configuration
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "TinyUSB initialized");
    
    // Create timer for key press/release cycle
    key_timer = xTimerCreate("key_timer", pdMS_TO_TICKS(1000), pdTRUE, NULL, key_timer_callback);
    if (key_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create timer");
        return;
    }
    
    // Start the timer
    if (xTimerStart(key_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start timer");
        return;
    }
    
    ESP_LOGI(TAG, "USB HID Keyboard started - pressing 'A' key every second");
    
    // Main loop - just keep the task alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}