#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#define NVS_NAMESPACE "odkey"

// WiFi Configuration
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASSWORD "wifi_pw"
#define NVS_KEY_WIFI_CONNECT_TIMEOUT "wifi_timeout"

// Http Server Configuration
#define NVS_KEY_HTTP_SERVER_PORT "http_port"

#ifdef __cplusplus
}
#endif

#endif  // NVS_CONFIG_H
