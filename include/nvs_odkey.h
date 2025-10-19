#ifndef NVS_ODKEY_H
#define NVS_ODKEY_H

#include <stdbool.h>
#include "nvs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NVS_NAMESPACE "odkey"

// WiFi Configuration
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASSWORD "wifi_pw"
#define NVS_KEY_WIFI_CONNECT_TIMEOUT "wifi_timeout"

// Http Service Configuration
#define NVS_KEY_HTTP_SERVER_PORT "http_port"

// mDNS Configuration
#define NVS_KEY_MDNS_HOSTNAME "mdns_hostname"
#define NVS_KEY_MDNS_INSTANCE "mdns_instance"

/**
 * @brief Initialize the NVS ODKey module
 *        This initializes NVS flash and ensures the ODKey namespace exists
 * @return true on success, false on failure
 */
bool nvs_odkey_init(void);

#ifdef __cplusplus
}
#endif

#endif  // NVS_ODKEY_H
