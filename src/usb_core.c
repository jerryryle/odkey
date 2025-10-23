#include "usb_core.h"
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "usb_keyboard.h"
#include "usb_system_config.h"

static const char *TAG = "usb_core";

#define TINYUSB_TASK_PRIORITY 7

#define RAW_HID_REPORT_SIZE 64  // 64 bytes for full flash alignment

/************* TinyUSB descriptors ****************/
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN)

/**
 * @brief Keyboard HID report descriptor (Interface 0)
 */
const uint8_t keyboard_report_descriptor[] = {TUD_HID_REPORT_DESC_KEYBOARD()};

/**
 * @brief Raw HID report descriptor (Interface 1)
 */
const uint8_t raw_hid_report_descriptor[] = {
    0x06,
    0x00,
    0xFF,  // Usage Page (Vendor Defined)
    0x09,
    0x01,  // Usage (Vendor Usage 1)
    0xA1,
    0x01,  // Collection (Application)

    // 64-byte raw HID report
    0x09,
    0x02,  // Usage (Vendor Usage 2)
    0x15,
    0x00,  // Logical Minimum (0)
    0x26,
    0xFF,
    0x00,  // Logical Maximum (255)
    0x75,
    0x08,  // Report Size (8 bits)
    0x95,
    RAW_HID_REPORT_SIZE,  // Report Count (64 bytes)
    0x91,
    0x02,  // Output (Data, Variable, Absolute)
    0x81,
    0x02,  // Input (Data, Variable, Absolute)

    0xC0  // End Collection
};

/**
 * @brief String descriptor
 */
const char *hid_string_descriptor[6] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},           // 0: is supported language is English (0x0409)
    "Jerry",                        // 1: Manufacturer
    "ODKey",                        // 2: Product
    "123456",                       // 3: Serial #
    "ODKey Keyboard",               // 4: HID Keyboard
    "ODKey Programming Interface",  // 5: HID Programming
};

/**
 * @brief Configuration descriptor
 *
 * This defines 1 configuration with 2 HID interfaces
 */
static const uint8_t hid_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute,
    // power in mA
    TUD_CONFIG_DESCRIPTOR(
        1, 2, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface 0: Keyboard (boot protocol)
    TUD_HID_DESCRIPTOR(USB_KEYBOARD_INTERFACE_NUM,
                       4,
                       HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(keyboard_report_descriptor),
                       0x81,
                       16,
                       1),

    // Interface 1: Raw HID (no boot protocol)
    TUD_HID_DESCRIPTOR(USB_SYSTEM_CONFIG_INTERFACE_NUM,
                       5,
                       HID_ITF_PROTOCOL_NONE,
                       sizeof(raw_hid_report_descriptor),
                       0x82,
                       64,
                       1),
};

/********* TinyUSB HID callbacks ***************/

// Invoked when GET HID REPORT DESCRIPTOR request is received
// Application returns a pointer to the descriptor
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    if (instance == 0) {
        return keyboard_report_descriptor;
    } else if (instance == 1) {
        return raw_hid_report_descriptor;
    }
    return NULL;
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance,
                               uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer,
                               uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer,
                           uint16_t bufsize) {
    (void)report_type;
    (void)report_id;  // No report IDs in new protocol

    // Route to appropriate module based on interface
    if (instance == USB_KEYBOARD_INTERFACE_NUM) {
        // Interface 0: Keyboard - no incoming reports expected
        ESP_LOGW(TAG, "Unexpected report on keyboard interface");
    } else if (instance == USB_SYSTEM_CONFIG_INTERFACE_NUM) {
        // Interface 1: Raw HID - route to program upload module
        usb_system_config_process_command(buffer, bufsize);
    }
}

// Invoked when received SET_PROTOCOL request
void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol) {
    (void)instance;
    (void)protocol;
}

static void device_event_handler(tinyusb_event_t *event, void *arg) {
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        ESP_LOGI(TAG, "USB device attached");
        break;
    case TINYUSB_EVENT_DETACHED:
        ESP_LOGI(TAG, "USB device detached");
        break;
    default:
        ESP_LOGW(TAG, "Unknown USB event: %d", event->id);
        break;
    }
}

bool usb_core_init(void) {
    // Initialize TinyUSB with default configuration
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(device_event_handler);

    // Override TinyUSB task priority to be higher than keyboard task
    tusb_cfg.task.priority = TINYUSB_TASK_PRIORITY;

    // Override specific descriptor fields
    tusb_cfg.descriptor.device = NULL;  // Use default device descriptor
    tusb_cfg.descriptor.full_speed_config = hid_configuration_descriptor;
    tusb_cfg.descriptor.string = hid_string_descriptor;
    tusb_cfg.descriptor.string_count =
        sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]);
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.high_speed_config = hid_configuration_descriptor;
#endif  // TUD_OPT_HIGH_SPEED

    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install TinyUSB driver: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "USB core initialized successfully");

    return true;
}
