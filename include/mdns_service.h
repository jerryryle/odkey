#ifndef MDNS_SERVICE_H
#define MDNS_SERVICE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the mDNS service module
 *        This initializes mDNS and loads configuration from NVS
 * @return true on success, false on failure
 */
bool mdns_service_init(void);

#ifdef __cplusplus
}
#endif

#endif  // MDNS_SERVICE_H
