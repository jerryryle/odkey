#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Button press callback function type
 */
typedef void (*button_callback_t)(void);

/**
 * @brief Initialize the button handler module
 * @param gpio_pin GPIO pin number for the button
 * @param debounce_ms Debounce time in milliseconds
 * @param callback Function to call when button is pressed (debounced)
 * @return true on success, false on failure
 */
bool button_handler_init(uint8_t gpio_pin, uint32_t debounce_ms, button_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_HANDLER_H
