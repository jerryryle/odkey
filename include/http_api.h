#ifndef HTTP_API_H
#define HTTP_API_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the HTTP API module
 *        This starts WiFi connection and HTTP server in a dedicated task
 * @return true on success, false on failure
 */
bool http_api_init(void);

/**
 * @brief Check if HTTP API is ready (WiFi connected and server running)
 * @return true if ready, false otherwise
 */
bool http_api_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif  // HTTP_API_H
