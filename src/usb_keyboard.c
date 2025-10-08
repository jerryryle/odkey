#include "usb_keyboard.h"
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
#include "driver/gpio.h"

static const char *TAG = "usb_keyboard";

// Private variables
static bool s_initialized = false;
static bool s_auto_typing_active = false;
static TimerHandle_t s_key_timer = NULL;
static uint8_t s_current_key = 0;
static bool s_key_pressed = false;

// Timer callback function
static void key_timer_callback(TimerHandle_t xTimer) {
    if (tud_hid_ready() && s_auto_typing_active) {
        if (!s_key_pressed) {
            // Press the key
            uint8_t keycode[6] = {s_current_key, 0, 0, 0, 0, 0};
            tud_hid_keyboard_report(0, 0, keycode);
            ESP_LOGI(TAG, "Key 0x%02X pressed", s_current_key);
            s_key_pressed = true;
        } else {
            // Release the key
            uint8_t keycode[6] = {0, 0, 0, 0, 0, 0};
            tud_hid_keyboard_report(0, 0, keycode);
            ESP_LOGI(TAG, "Key 0x%02X released", s_current_key);
            s_key_pressed = false;
        }
    }
}

/************* TinyUSB descriptors ****************/
#define TUSB_DESC_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

/**
 * @brief HID report descriptor
 *
 * In this example we implement Keyboard + Mouse HID device,
 * so we must define both report descriptors
 */
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD))
};

/**
 * @brief String descriptor
 */
const char *hid_string_descriptor[5] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},  // 0: is supported language is English (0x0409)
    "TinyUSB",             // 1: Manufacturer
    "TinyUSB Device",      // 2: Product
    "123456",              // 3: Serials, should use chip ID
    "Example HID interface",  // 4: HID
};

/**
 * @brief Configuration descriptor
 *
 * This is a simple configuration descriptor that defines 1 configuration and 1 HID interface
 */
static const uint8_t hid_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
};

/********* TinyUSB HID callbacks ***************/

// Invoked when GET HID REPORT DESCRIPTOR request is received
// Application returns a pointer to the descriptor
uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance) {
    // We use only one interface and one HID report descriptor, so we can ignore parameter 'instance'
    (void) instance;
    return hid_report_descriptor;
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

bool usb_keyboard_init(void) {
    if (s_initialized) {
        return true;
    }

    // Initialize TinyUSB with default configuration
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    
    // Override specific descriptor fields
    tusb_cfg.descriptor.device = NULL;  // Use default device descriptor
    tusb_cfg.descriptor.full_speed_config = hid_configuration_descriptor;
    tusb_cfg.descriptor.string = hid_string_descriptor;
    tusb_cfg.descriptor.string_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]);
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.high_speed_config = hid_configuration_descriptor;
#endif // TUD_OPT_HIGH_SPEED        

    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
        
    if (ret != ESP_OK) {
        return false;
    }
    
    s_initialized = true;
    
    // Test: Send single 'A' key press once device is ready
    ESP_LOGI(TAG, "Waiting for USB device to be ready...");
    while (!tud_hid_ready()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(TAG, "USB device ready! Sending single 'A' key press...");
    
    // Send single 'A' key press
    uint8_t keycode[6] = {0x04, 0, 0, 0, 0, 0};
    tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, keycode);
    
    // Wait a bit
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Release the key
    uint8_t empty_keycode[6] = {0, 0, 0, 0, 0, 0};
    tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, empty_keycode);
    
    ESP_LOGI(TAG, "Key press/release sent. Done.");
    
    return true;
}

bool usb_keyboard_start_auto_typing(uint8_t key_code, uint32_t interval_ms) {
    if (!s_initialized) {
        ESP_LOGE(TAG, "USB keyboard not initialized");
        return false;
    }

    if (s_auto_typing_active) {
        ESP_LOGW(TAG, "Auto-typing already active");
        return true;
    }

    // Create timer for key press/release cycle
    s_key_timer = xTimerCreate("key_timer", pdMS_TO_TICKS(interval_ms), pdTRUE, NULL, key_timer_callback);
    if (s_key_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create timer");
        return false;
    }

    s_current_key = key_code;
    s_auto_typing_active = true;
    s_key_pressed = false;

    // Start the timer
    if (xTimerStart(s_key_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start timer");
        xTimerDelete(s_key_timer, 0);
        s_key_timer = NULL;
        s_auto_typing_active = false;
        return false;
    }

    ESP_LOGI(TAG, "Auto-typing started - pressing key 0x%02X every %lu ms", key_code, interval_ms);
    return true;
}

void usb_keyboard_stop_auto_typing(void) {
    if (!s_auto_typing_active) {
        return;
    }

    if (s_key_timer != NULL) {
        xTimerStop(s_key_timer, 0);
        xTimerDelete(s_key_timer, 0);
        s_key_timer = NULL;
    }

    s_auto_typing_active = false;
    s_key_pressed = false;
    ESP_LOGI(TAG, "Auto-typing stopped");
}

bool usb_keyboard_press_key(uint8_t key_code) {
    if (!s_initialized || !tud_hid_ready()) {
        return false;
    }

    uint8_t keycode[6] = {key_code, 0, 0, 0, 0, 0};
    tud_hid_keyboard_report(0, 0, keycode);
    ESP_LOGI(TAG, "Key 0x%02X pressed manually", key_code);
    return true;
}

bool usb_keyboard_release_keys(void) {
    if (!s_initialized || !tud_hid_ready()) {
        return false;
    }

    uint8_t keycode[6] = {0, 0, 0, 0, 0, 0};
    tud_hid_keyboard_report(0, 0, keycode);
    ESP_LOGI(TAG, "All keys released");
    return true;
}

bool usb_keyboard_is_ready(void) {
    return s_initialized && tud_hid_ready();
}

void usb_keyboard_deinit(void) {
    if (!s_initialized) {
        return;
    }

    usb_keyboard_stop_auto_typing();
    
    // Note: TinyUSB doesn't have an uninstall function in this version
    // The driver will be cleaned up when the system shuts down
    s_initialized = false;
    ESP_LOGI(TAG, "USB keyboard deinitialized");
}
