#ifndef BUTTON_H
#define BUTTON_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the button module
 * @param gpio_pin GPIO pin number for the button
 * @param debounce_ms Debounce time in milliseconds
 * @return true on success, false on failure
 */
bool button_init(uint8_t gpio_pin, uint32_t debounce_ms);

#ifdef __cplusplus
}
#endif

#endif  // BUTTON_H
