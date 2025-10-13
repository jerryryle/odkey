#ifndef USB_KEYBOARD_H
#define USB_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the USB keyboard module
 * @param interface_num USB HID interface number to use for keyboard reports
 * @return true on success, false on failure
 */
bool usb_keyboard_init(uint8_t interface_num);

/**
 * @brief Send keycodes to the USB keyboard
 * @param modifier Modifier key to send (or 0 to release all modifiers)
 * @param keys Array of keycodes to send (can be NULL to release all keys)
 * @param count Number of keycodes in the array (0-6, more than 6 will be truncated)
 * @return true on success, false on failure
 */
bool usb_keyboard_send_keys(uint8_t modifier, const uint8_t* keys, uint8_t count);

/**
 * @brief Check if USB keyboard is ready
 * @return true if ready, false otherwise
 */
bool usb_keyboard_is_ready(void);



#ifdef __cplusplus
}
#endif

#endif // USB_KEYBOARD_H
