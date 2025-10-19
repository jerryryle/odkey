#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the HTTP server module
 * @return true on success, false on failure
 */
bool http_server_init(void);

/**
 * @brief Get the HTTP server port
 * @return HTTP server port
 */
uint16_t http_server_get_port(void);

/**
 * @brief Start the HTTP server
 * @return true on success, false on failure
 */
esp_err_t http_server_start(void);

/**
 * @brief Stop the HTTP server
 */
esp_err_t http_server_stop(void);

/**
 * @brief Check if HTTP server is running
 * @return true if running, false otherwise
 */
bool http_server_is_running(void);

#ifdef __cplusplus
}
#endif

#endif  // HTTP_SERVER_H
