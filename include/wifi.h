#ifndef WIFI_H
#define WIFI_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the WiFi module
 *        This initializes WiFi driver and configuration but does not start connection
 * @return true on success, false on failure
 */
bool wifi_init(void);

/**
 * @brief Start WiFi connection
 *        This starts WiFi connection management in a dedicated task
 * @return true on success, false on failure
 */
bool wifi_start(void);

/**
 * @brief Check if WiFi is connected
 * @return true if connected, false otherwise
 */
bool wifi_is_connected(void);

/**
 * @brief Get the current IP address as a string
 * @return IP address string, or empty string if not connected
 */
const char *wifi_get_ip_address(void);

#ifdef __cplusplus
}
#endif

#endif  // WIFI_H
