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
 * @return true on success, false on failure
 * @note Debounce and repeat delay values are read from NVS with defaults of 50ms and
 * 250ms
 */
bool button_init(uint8_t gpio_pin);

#ifdef __cplusplus
}
#endif

#endif  // BUTTON_H
