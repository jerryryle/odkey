#ifndef HTTP_SERVICE_H
#define HTTP_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the HTTP service module
 *        This registers event handlers and loads configuration
 * @return true on success, false on failure
 */
bool http_service_init(void);

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
