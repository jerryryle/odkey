#ifndef NVS_ODKEY_H
#define NVS_ODKEY_H

#include <stdbool.h>
#include "nvs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NVS_NAMESPACE "odkey"

// The following keys are used to store configuration values in NVS. They must be 15
// characters or less.

// WiFi Configuration
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASSWORD "wifi_pw"

// Http Service Configuration
#define NVS_KEY_HTTP_SERVER_PORT "http_port"

// mDNS Configuration
#define NVS_KEY_MDNS_HOSTNAME "mdns_hostname"
#define NVS_KEY_MDNS_INSTANCE "mdns_instance"

// HTTP API Configuration
#define NVS_KEY_HTTP_API_KEY "http_api_key"

// Button Configuration
#define NVS_KEY_BUTTON_DEBOUNCE_MS "button_debounce"
#define NVS_KEY_BUTTON_REPEAT_DELAY_MS "button_repeat"

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
