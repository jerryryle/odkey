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

// Helper function to safely convert report type to string
static const char *report_type_to_string(uint8_t report_type) {
    switch (report_type) {
    case HID_REPORT_TYPE_INVALID:
        return "INVALID";
    case HID_REPORT_TYPE_INPUT:
        return "INPUT";
    case HID_REPORT_TYPE_OUTPUT:
        return "OUTPUT";
    case HID_REPORT_TYPE_FEATURE:
        return "FEATURE";
    default:
        return "UNKNOWN";
    }
}

#define TINYUSB_TASK_PRIORITY 7

#define RAW_HID_REPORT_SIZE 64  // 64 bytes for full flash alignment

/************* TinyUSB descriptors ****************/
#define TUSB_DESC_TOTAL_LEN \
    (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

/**
 * @brief Keyboard HID report descriptor (Interface 0)
 */
const uint8_t keyboard_report_descriptor[] = {TUD_HID_REPORT_DESC_KEYBOARD()};

/**
 * @brief Raw HID report descriptor (Interface 1)
 */
// clang-format off
const uint8_t raw_hid_report_descriptor[] = {
    0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined)
    0x09, 0x01,        // Usage (Vendor Usage 1)
    0xA1, 0x01,        // Collection (Application)

    // Input report (device to host)
    0x09, 0x02,       // Usage (Vendor Usage 2)
    0x15, 0x00,       // Logical Minimum (0)
    0x26, 0xFF, 0x00, // Logical Maximum (255)
    0x75, 0x08,       // Report Size (8 bits)
    0x95, RAW_HID_REPORT_SIZE,  // Report Count (64 bytes)
    0x81, 0x02,       // Input (Data, Variable, Absolute)

    // Output report (host to device)
    0x09, 0x03,       // Usage (Vendor Usage 2)
    0x15, 0x00,       // Logical Minimum (0)
    0x26, 0xFF, 0x00, // Logical Maximum (255)
    0x75, 0x08,       // Report Size (8 bits)
    0x95, RAW_HID_REPORT_SIZE,  // Report Count (64 bytes)
    0x91, 0x02,       // Output (Data, Variable, Absolute)

    0xC0,             // End Collection
};
// clang-format on

/**
 * @brief String descriptor
 */
const char *hid_string_descriptor[] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},           // 0: is supported language is English (0x0409)
    "JerryDesign",                  // 1: Manufacturer
    "ODKey Keyboard",               // 2: Product
    "123456",                       // 3: Serial #
    "ODKey HID Keyboard",           // 4: HID Keyboard
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
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUSB_DESC_TOTAL_LEN, 0, 100),

    // Interface 0: Keyboard (boot protocol)
    TUD_HID_DESCRIPTOR(USB_KEYBOARD_INTERFACE_NUM,
                       4,
                       HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(keyboard_report_descriptor),
                       0x81,  // EP In address
                       8,     // EP size
                       10),   // EP interval

    // Interface 1: Raw HID (no boot protocol)
    TUD_HID_INOUT_DESCRIPTOR(USB_SYSTEM_CONFIG_INTERFACE_NUM,
                             5,
                             HID_ITF_PROTOCOL_NONE,
                             sizeof(raw_hid_report_descriptor),
                             0x82,  // EP In address
                             0x02,  // EP Out address
                             64,    // EP size
                             1),    // EP interval
};

// Device descriptor
static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,      // USB 2.0
    .bDeviceClass = 0x00,  // Interface-specific class
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x05AC,     // Vendor ID (Apple)
    .idProduct = 0x0250,    // Product ID (Apple Internal Keyboard)
    .bcdDevice = 0x0100,    // Device version
    .iManufacturer = 0x01,  // String index for manufacturer
    .iProduct = 0x02,       // String index for product
    .iSerialNumber = 0x03,  // String index for serial
    .bNumConfigurations = 0x01,
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
    (void)report_id;

    // Route to appropriate module based on interface
    if (instance == USB_KEYBOARD_INTERFACE_NUM) {
        // Interface 0: Keyboard - handle LED reports
        if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= 1) {
            // LED status report received
            uint8_t led_status = buffer[0];
            ESP_LOGI(TAG, "Keyboard LED status: 0x%02X", led_status);
            // You could store this and use it to control physical LEDs
        } else if (report_type == HID_REPORT_TYPE_FEATURE) {
            // FEATURE reports are configuration requests - acknowledge silently
            ESP_LOGI(TAG,
                     "Keyboard FEATURE report received: size=%d, data=[%02X %02X %02X]",
                     bufsize,
                     bufsize > 0 ? buffer[0] : 0,
                     bufsize > 1 ? buffer[1] : 0,
                     bufsize > 2 ? buffer[2] : 0);

            // Check if this looks like a SET_PROTOCOL request
            if (bufsize >= 1 && buffer[0] <= 1) {
                ESP_LOGI(TAG,
                         "This FEATURE report appears to be macOS requesting a "
                         "protocol switch to: %s Protocol",
                         buffer[0] ? "Report" : "Boot");
            }
        } else {
            ESP_LOGW(TAG,
                     "Unexpected %s report on keyboard interface: size=%d, "
                     "data=[%02X %02X %02X %02X %02X %02X %02X %02X]",
                     report_type_to_string(report_type),
                     bufsize,
                     bufsize > 0 ? buffer[0] : 0,
                     bufsize > 1 ? buffer[1] : 0,
                     bufsize > 2 ? buffer[2] : 0,
                     bufsize > 3 ? buffer[3] : 0,
                     bufsize > 4 ? buffer[4] : 0,
                     bufsize > 5 ? buffer[5] : 0,
                     bufsize > 6 ? buffer[6] : 0,
                     bufsize > 7 ? buffer[7] : 0);
        }
    } else if (instance == USB_SYSTEM_CONFIG_INTERFACE_NUM) {
        // Interface 1: Raw HID - route to program upload module
        usb_system_config_process_command(buffer, bufsize);
    }
}

// Invoked when received SET_PROTOCOL request
void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol) {
    ESP_LOGI(TAG, "SET_PROTOCOL request: instance=%d, protocol=%d", instance, protocol);
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
    tusb_cfg.descriptor.device = &device_descriptor;
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
