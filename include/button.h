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
 * @param repeat_delay_ms Delay between program restarts when button is held (in
 * milliseconds)
 * @return true on success, false on failure
 */
bool button_init(uint8_t gpio_pin, uint32_t debounce_ms, uint32_t repeat_delay_ms);

#ifdef __cplusplus
}
#endif

#endif  // BUTTON_H
