#ifndef PROGRAM_UPLOAD_H
#define PROGRAM_UPLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback function type for handling program upload start events
 * @return true to allow upload to proceed, false to abort upload
 */
typedef bool (*program_upload_start_callback_t)(void);

/**
 * @brief Initialize the program upload module
 * @param interface_num USB HID interface number to use for Raw HID reports
 * @param on_upload_start Callback called when program upload starts (can be NULL)
 * @return true on success, false on failure
 */
bool program_upload_init(uint8_t interface_num, program_upload_start_callback_t on_upload_start);

/**
 * @brief Process incoming command from host
 * @param data Command data (command code in first byte)
 * @param len Length of data
 */
void program_upload_process_command(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif  // PROGRAM_UPLOAD_H
