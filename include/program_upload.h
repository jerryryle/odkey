#ifndef PROGRAM_UPLOAD_H
#define PROGRAM_UPLOAD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the program upload module
 * @param interface_num USB HID interface number to use for Raw HID reports
 * @return true on success, false on failure
 */
bool program_upload_init(uint8_t interface_num);

/**
 * @brief Process incoming command from host
 * @param report_id Report ID indicating command type
 * @param data Command data
 * @param len Length of data
 */
void program_upload_process_command(uint8_t report_id, const uint8_t* data, uint16_t len);



#ifdef __cplusplus
}
#endif

#endif // PROGRAM_UPLOAD_H
