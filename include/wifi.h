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

#ifdef __cplusplus
}
#endif

#endif  // WIFI_H
