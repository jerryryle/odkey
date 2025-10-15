#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

// WiFi Station Mode Configuration (defaults - can be overridden in NVS)
#define WIFI_SSID_DEFAULT "ODKey-Default"
#define WIFI_PASSWORD_DEFAULT "odkey123"

// NVS Keys for WiFi credentials
#define NVS_NAMESPACE "wifi_config"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASSWORD "password"

// WiFi Connection Retry Configuration
#define WIFI_RETRY_INTERVAL_MS 10000  // Retry every 10 seconds

// WiFi Connection Configuration
#define WIFI_CONNECT_TIMEOUT_MS 10000

// HTTP Server Configuration
#define HTTP_SERVER_PORT 80
#define HTTP_SERVER_MAX_URI_LEN 512
#define HTTP_SERVER_MAX_REQ_HDR_LEN 512

#ifdef __cplusplus
}
#endif

#endif // WIFI_CONFIG_H
