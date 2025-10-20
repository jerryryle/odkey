#ifndef HTTP_SERVICE_H
#define HTTP_SERVICE_H

#include <stdbool.h>
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
 * @brief Initialize the HTTP service module
 *        This registers event handlers and loads configuration
 * @param on_upload_start Callback called when program upload starts (can be NULL)
 * @return true on success, false on failure
 */
bool http_service_init(program_upload_start_callback_t on_upload_start);

/**
 * @brief Get the HTTP service port
 * @return HTTP service port
 */
uint16_t http_service_get_port(void);

/**
 * @brief Check if HTTP service is running
 * @return true if running, false otherwise
 */
bool http_service_is_running(void);

#ifdef __cplusplus
}
#endif

#endif  // HTTP_SERVICE_H
