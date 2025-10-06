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
 * @brief Start the USB keyboard auto-typing functionality
 * @param key_code HID key code to press (e.g., 0x04 for 'A')
 * @param interval_ms Interval between key press/release cycles in milliseconds
 * @return true on success, false on failure
 */
bool usb_keyboard_start_auto_typing(uint8_t key_code, uint32_t interval_ms);

/**
 * @brief Stop the USB keyboard auto-typing functionality
 */
void usb_keyboard_stop_auto_typing(void);

/**
 * @brief Manually press a key
 * @param key_code HID key code to press
 * @return true on success, false on failure
 */
bool usb_keyboard_press_key(uint8_t key_code);

/**
 * @brief Release all keys
 * @return true on success, false on failure
 */
bool usb_keyboard_release_keys(void);

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
