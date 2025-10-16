#ifndef USB_CORE_H
#define USB_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Interface numbers for USB HID interfaces
 */
#define USB_KEYBOARD_INTERFACE_NUM 0
#define USB_PROGRAM_UPLOAD_INTERFACE_NUM 1

/**
 * @brief Initialize the USB core module
 * @return true on success, false on failure
 */
bool usb_core_init(void);

/**
 * @brief Check if USB core is ready
 * @return true if ready, false otherwise
 */
bool usb_core_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif  // USB_CORE_H
