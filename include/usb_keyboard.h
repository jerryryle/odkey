#ifndef USB_KEYBOARD_H
#define USB_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the USB keyboard module
 * @return true on success, false on failure
 */
bool usb_keyboard_init(void);

/**
 * @brief Send keycodes to the USB keyboard
 * @param keycodes Array of keycodes to send (can be NULL to release all keys)
 * @param count Number of keycodes in the array (0-6, more than 6 will be truncated)
 * @return true on success, false on failure
 */
bool usb_keyboard_send_keycodes(const uint8_t* keycodes, uint8_t count);

/**
 * @brief Check if USB keyboard is ready
 * @return true if ready, false otherwise
 */
bool usb_keyboard_is_ready(void);

/**
 * @brief Deinitialize the USB keyboard module
 */
void usb_keyboard_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // USB_KEYBOARD_H
