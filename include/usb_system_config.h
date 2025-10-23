#ifndef USB_SYSTEM_CONFIG_H
#define USB_SYSTEM_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the program upload module
 * @param interface_num USB HID interface number to use for Raw HID reports
 * @return true on success, false on failure
 */
bool usb_system_config_init(uint8_t interface_num);

/**
 * @brief Process incoming command from host
 * @param data Command data (command code in first byte)
 * @param len Length of data
 */
void usb_system_config_process_command(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif  // USB_SYSTEM_CONFIG_H
