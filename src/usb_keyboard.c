#include "usb_keyboard.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "usb_keyboard_keys.h"

static const char *TAG = "usb_keyboard";

// Private variables
static bool g_initialized = false;
static uint8_t g_interface_num = 0;

#define HID_KEYBOARD_REPORT_ID 1

bool usb_keyboard_init(uint8_t interface_num) {
    if (g_initialized) {
        return true;
    }

    g_interface_num = interface_num;
    g_initialized = true;
    ESP_LOGI(TAG, "USB keyboard module initialized on interface %d", interface_num);

    return true;
}

bool usb_keyboard_send_keys(uint8_t modifier, const uint8_t *keys, uint8_t count) {
    if (!g_initialized) {
        return false;
    }

    uint8_t keycode_array[6] = {0, 0, 0, 0, 0, 0};

    if (keys != NULL) {
        // Copy keycodes to array (up to 6)
        for (uint8_t i = 0; i < count && i < 6; i++) {
            keycode_array[i] = keys[i];
        }
    }

    tud_hid_n_keyboard_report(g_interface_num, HID_KEYBOARD_REPORT_ID, modifier, keycode_array);
    return true;
}

bool usb_keyboard_is_ready(void) {
    return g_initialized && tud_hid_ready();
}
