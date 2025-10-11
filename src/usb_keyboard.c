#include "usb_keyboard.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "tinyusb_default_config.h"
#include "tinyusb.h"
#include "sdkconfig.h"
#include "driver/gpio.h"

static const char *TAG = "usb_keyboard";

// Private variables
static bool s_initialized = false;

#define HID_KEYBOARD_REPORT_ID 1

/************* TinyUSB descriptors ****************/
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

/**
 * @brief HID report descriptor
 *
 * In this example we implement Keyboard + Mouse HID device,
 * so we must define both report descriptors
 */
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_KEYBOARD_REPORT_ID))
};

/**
 * @brief String descriptor
 */
const char *hid_string_descriptor[5] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},   // 0: is supported language is English (0x0409)
    "Jerry",                // 1: Manufacturer
    "ODKey",                // 2: Product
    "123456",               // 3: Serial #
    "ODKey Keyboard",       // 4: HID
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
    TUD_HID_DESCRIPTOR(0, 4, HID_ITF_PROTOCOL_KEYBOARD, sizeof(hid_report_descriptor), 0x81, 16, 10),
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
    ESP_LOGI(TAG, "USB keyboard initialized successfully");
    
    return true;
}


bool usb_keyboard_send_keys(uint8_t modifier, const uint8_t* keycodes, uint8_t count) {
    if (!s_initialized || !tud_hid_ready()) {
        return false;
    }

    uint8_t keycode_array[6] = {0, 0, 0, 0, 0, 0};

    if (keycodes != NULL) {
        // Copy keycodes to array (up to 6)
        for (uint8_t i = 0; i < count && i < 6; i++) {
                keycode_array[i] = keycodes[i];
            }
    }
    
    while (!tud_hid_ready()) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    tud_hid_keyboard_report(HID_KEYBOARD_REPORT_ID, modifier, keycode_array);
    return true;
}


bool usb_keyboard_is_ready(void) {
    return s_initialized && tud_hid_ready();
}

void usb_keyboard_deinit(void) {
    if (!s_initialized) {
        return;
    }
    
    // Note: TinyUSB doesn't have an uninstall function in this version
    // The driver will be cleaned up when the system shuts down
    s_initialized = false;
    ESP_LOGI(TAG, "USB keyboard deinitialized");
}
