#include "http_service.h"
#include <string.h>
#include <sys/time.h>
#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs_odkey.h"
#include "program.h"
#include "wifi.h"

static const char *TAG = "http_service";

// Configuration defaults
#define HTTP_SERVICE_PORT_DEFAULT 80

// HTTP Service Configuration
#define HTTP_SERVICE_MAX_URI_HANDLERS 16
#define HTTP_SERVICE_MAX_RESP_HEADERS 8
#define HTTP_SERVICE_MAX_OPEN_SOCKETS \
    1  // Single connection to prevent concurrent uploads

struct http_service_config_t {
    uint16_t service_port;
    char api_key[64];
} g_http_service_config = {0};

// HTTP service handle
static httpd_handle_t g_service = NULL;

// Global buffer for all HTTP operations
#define HTTP_SERVICE_RESPONSE_BUFFER_SIZE 4096
#define HTTP_SERVICE_WORKING_BUFFER_SIZE 4096
static uint8_t g_response_buffer[HTTP_SERVICE_RESPONSE_BUFFER_SIZE];
static uint8_t g_working_buffer[HTTP_SERVICE_WORKING_BUFFER_SIZE];

// Event handler instances
static esp_event_handler_instance_t g_wifi_event_instance = NULL;
static esp_event_handler_instance_t g_ip_event_instance = NULL;

// Forward declarations
static esp_err_t start_http_service(void);
static void stop_http_service(void);

// Get HTTP service configuration from NVS or use defaults
static bool load_http_service_configuration(struct http_service_config_t *config) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    memset(config, 0, sizeof(*config));

    // Try to open NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS");
        return false;
    }

    // Try to get service port
    err = nvs_get_u16(nvs_handle, NVS_KEY_HTTP_SERVER_PORT, &config->service_port);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Found service port in NVS: %d", config->service_port);
    } else {
        ESP_LOGI(TAG, "Service port not found in NVS, using default");
        config->service_port = HTTP_SERVICE_PORT_DEFAULT;
    }

    // Try to get API key
    size_t required_size = sizeof(config->api_key);
    err =
        nvs_get_str(nvs_handle, NVS_KEY_HTTP_API_KEY, config->api_key, &required_size);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Found API key in NVS");
    } else {
        ESP_LOGI(TAG, "API key not found in NVS, APIs will be disabled");
        config->api_key[0] = '\0';
    }

    nvs_close(nvs_handle);
    return true;
}

// Authentication middleware
static esp_err_t check_api_key(httpd_req_t *req) {
    // If no API key is configured, return 401
    if (strlen(g_http_service_config.api_key) == 0) {
        ESP_LOGW(TAG, "API key not configured, rejecting request");
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"API key not configured\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Get Authorization header
    size_t auth_header_len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (auth_header_len == 0) {
        ESP_LOGW(TAG, "Missing Authorization header");
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Missing Authorization header\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Use working buffer for header value
    if (auth_header_len + 1 > HTTP_SERVICE_WORKING_BUFFER_SIZE) {
        ESP_LOGE(
            TAG, "Authorization header too large: %lu", (unsigned long)auth_header_len);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
                        "{\"error\":\"Authorization header too large\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    char *auth_header = (char *)g_working_buffer;

    // Get the header value
    if (httpd_req_get_hdr_value_str(
            req, "Authorization", auth_header, auth_header_len + 1) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get Authorization header");
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Invalid Authorization header\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Check for Bearer token format
    const char *bearer_prefix = "Bearer ";
    if (strncmp(auth_header, bearer_prefix, strlen(bearer_prefix)) != 0) {
        ESP_LOGW(TAG, "Invalid Authorization header format");
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
                        "{\"error\":\"Invalid Authorization header format\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Extract token
    const char *token = auth_header + strlen(bearer_prefix);
    if (strcmp(token, g_http_service_config.api_key) != 0) {
        ESP_LOGW(TAG, "Invalid API key");
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Invalid API key\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    return ESP_OK;
}

// Program upload handler - POST /api/program/flash
static esp_err_t flash_program_upload_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Flash program upload request received");

    // Check authentication
    if (check_api_key(req) != ESP_OK) {
        return ESP_FAIL;
    }

    // Get content length
    size_t content_length = req->content_len;
    if (content_length == 0) {
        ESP_LOGE(TAG, "Missing Content-Length header");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
                        "{\"error\":\"Missing Content-Length header\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    if (content_length > PROGRAM_FLASH_MAX_SIZE) {
        ESP_LOGE(
            TAG, "Flash program too large: %lu bytes", (unsigned long)content_length);
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Program too large\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Start program storage write session
    if (!program_write_start(
            PROGRAM_TYPE_FLASH, content_length, PROGRAM_WRITE_SOURCE_HTTP)) {
        ESP_LOGE(TAG, "Failed to start flash program storage write session");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
                        "{\"error\":\"Failed to start flash program storage\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Get session buffer for this connection
    uint8_t *buffer = g_working_buffer;
    size_t buffer_size = HTTP_SERVICE_WORKING_BUFFER_SIZE;

    // Read and write program data in chunks
    size_t bytes_remaining = content_length;

    while (bytes_remaining > 0) {
        size_t chunk_size =
            (bytes_remaining > buffer_size) ? buffer_size : bytes_remaining;

        // Read chunk from HTTP request
        int ret = httpd_req_recv(req, (char *)buffer, chunk_size);
        if (ret <= 0) {
            ESP_LOGE(TAG, "Failed to receive data chunk");
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(
                req, "{\"error\":\"Failed to receive data\"}", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }

        // Write chunk to program storage
        if (!program_write_chunk(
                PROGRAM_TYPE_FLASH, buffer, ret, PROGRAM_WRITE_SOURCE_HTTP)) {
            ESP_LOGE(TAG, "Failed to write chunk to flash program");
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req,
                            "{\"error\":\"Failed to write to flash program\"}",
                            HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }

        bytes_remaining -= ret;
    }

    // Finish program storage write session
    if (!program_write_finish(
            PROGRAM_TYPE_FLASH, content_length, PROGRAM_WRITE_SOURCE_HTTP)) {
        ESP_LOGE(TAG, "Failed to finish flash program write session");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
                        "{\"error\":\"Failed to finish flash program\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "Flash program upload completed successfully: %lu bytes",
             (unsigned long)content_length);
    httpd_resp_set_type(req, "application/json");
    char response[64];
    snprintf(response,
             sizeof(response),
             "{\"success\":true,\"size\":%lu}",
             (unsigned long)content_length);
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Flash program download handler - GET /api/program/flash
static esp_err_t flash_program_download_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Flash program download request received");

    // Check authentication
    if (check_api_key(req) != ESP_OK) {
        return ESP_FAIL;
    }

    // Get flash program
    uint32_t program_size;
    const uint8_t *program_data = program_get(PROGRAM_TYPE_FLASH, &program_size);

    if (program_data == NULL || program_size == 0) {
        ESP_LOGW(TAG, "No program stored in flash");
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"No program found\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Set headers for file download
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(
        req, "Content-Disposition", "attachment; filename=\"program.bin\"");
    httpd_resp_set_hdr(req, "Content-Length", NULL);  // Will be set automatically

    // Send program data
    esp_err_t ret = httpd_resp_send(req, (const char *)program_data, program_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send flash program data");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "Flash program download completed: %lu bytes",
             (unsigned long)program_size);
    return ESP_OK;
}

// Flash program delete handler - DELETE /api/program/flash
static esp_err_t flash_program_delete_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Flash program delete request received");

    // Check authentication
    if (check_api_key(req) != ESP_OK) {
        return ESP_FAIL;
    }

    // Erase flash program
    if (!program_erase(PROGRAM_TYPE_FLASH)) {
        ESP_LOGE(TAG, "Failed to erase flash program");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
                        "{\"error\":\"Failed to erase flash program\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Flash program deleted successfully");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Flash program execute handler - POST /api/program/flash/execute
static esp_err_t flash_program_execute_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Flash program execute request received");

    // Check authentication
    if (check_api_key(req) != ESP_OK) {
        return ESP_FAIL;
    }

    if (!program_execute(PROGRAM_TYPE_FLASH, NULL, NULL)) {
        ESP_LOGW(TAG, "Flash program execution failed");
        httpd_resp_set_status(req, "422 Unprocessable Entity");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
                        "{\"error\":\"Flash program cannot be executed\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Flash program execution started");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// RAM program upload handler - POST /api/program/ram
static esp_err_t ram_program_upload_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "RAM program upload request received");

    // Check authentication
    if (check_api_key(req) != ESP_OK) {
        return ESP_FAIL;
    }

    // Get content length
    size_t content_length = req->content_len;
    if (content_length == 0) {
        ESP_LOGE(TAG, "Missing Content-Length header");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
                        "{\"error\":\"Missing Content-Length header\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    if (content_length > PROGRAM_RAM_MAX_SIZE) {
        ESP_LOGE(
            TAG, "RAM program too large: %lu bytes", (unsigned long)content_length);
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"RAM program too large\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Start RAM program storage write session
    if (!program_write_start(
            PROGRAM_TYPE_RAM, content_length, PROGRAM_WRITE_SOURCE_HTTP)) {
        ESP_LOGE(TAG, "Failed to start RAM program storage write session");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
                        "{\"error\":\"Failed to start RAM program storage\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Get session buffer for this connection
    uint8_t *buffer = g_working_buffer;
    size_t buffer_size = HTTP_SERVICE_WORKING_BUFFER_SIZE;

    // Read and write program data in chunks
    size_t bytes_remaining = content_length;

    while (bytes_remaining > 0) {
        size_t chunk_size =
            (bytes_remaining > buffer_size) ? buffer_size : bytes_remaining;

        // Read chunk from HTTP request
        int ret = httpd_req_recv(req, (char *)buffer, chunk_size);
        if (ret <= 0) {
            ESP_LOGE(TAG, "Failed to receive data chunk");
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(
                req, "{\"error\":\"Failed to receive data\"}", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }

        // Write chunk to RAM program storage
        if (!program_write_chunk(
                PROGRAM_TYPE_RAM, buffer, ret, PROGRAM_WRITE_SOURCE_HTTP)) {
            ESP_LOGE(TAG, "Failed to write chunk to RAM program storage");
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req,
                            "{\"error\":\"Failed to write to RAM program storage\"}",
                            HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }

        bytes_remaining -= ret;
    }

    // Finish RAM program storage write session
    if (!program_write_finish(
            PROGRAM_TYPE_RAM, content_length, PROGRAM_WRITE_SOURCE_HTTP)) {
        ESP_LOGE(TAG, "Failed to finish RAM program storage write session");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
                        "{\"error\":\"Failed to finish RAM program storage\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "RAM program upload completed successfully: %lu bytes",
             (unsigned long)content_length);
    httpd_resp_set_type(req, "application/json");
    char response[64];
    snprintf(response,
             sizeof(response),
             "{\"success\":true,\"size\":%lu}",
             (unsigned long)content_length);
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// RAM program download handler - GET /api/program/ram
static esp_err_t ram_program_download_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "RAM program download request received");

    // Check authentication
    if (check_api_key(req) != ESP_OK) {
        return ESP_FAIL;
    }

    // Get RAM program from storage
    uint32_t program_size;
    const uint8_t *program_data = program_get(PROGRAM_TYPE_RAM, &program_size);

    if (program_data == NULL || program_size == 0) {
        ESP_LOGW(TAG, "No RAM program stored");
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"No RAM program found\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Set headers for file download
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(
        req, "Content-Disposition", "attachment; filename=\"ram_program.bin\"");
    httpd_resp_set_hdr(req, "Content-Length", NULL);  // Will be set automatically

    // Send program data
    esp_err_t ret = httpd_resp_send(req, (const char *)program_data, program_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send RAM program data");
        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG, "RAM program download completed: %lu bytes", (unsigned long)program_size);
    return ESP_OK;
}

// RAM program delete handler - DELETE /api/program/ram
static esp_err_t ram_program_delete_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "RAM program delete request received");

    // Check authentication
    if (check_api_key(req) != ESP_OK) {
        return ESP_FAIL;
    }

    // Erase RAM program from storage
    if (!program_erase(PROGRAM_TYPE_RAM)) {
        ESP_LOGE(TAG, "Failed to erase RAM program from storage");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Failed to erase RAM program\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "RAM program deleted successfully");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// RAM program execute handler - POST /api/program/ram/execute
static esp_err_t ram_program_execute_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "RAM program execute request received");

    // Check authentication
    if (check_api_key(req) != ESP_OK) {
        return ESP_FAIL;
    }

    if (!program_execute(PROGRAM_TYPE_RAM, NULL, NULL)) {
        ESP_LOGW(TAG, "RAM program execution failed");
        httpd_resp_set_status(req, "422 Unprocessable Entity");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
                        "{\"error\":\"RAM program cannot be executed\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "RAM program execution started");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// NVS get handler - GET /api/nvs/{key}
static esp_err_t nvs_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "NVS get request received");

    // Check authentication
    if (check_api_key(req) != ESP_OK) {
        return ESP_FAIL;
    }

    // Extract key from URI (skip "/api/nvs/" prefix)
    const char *uri = req->uri;
    const char *key = uri + 9;  // Skip "/api/nvs/"

    if (strlen(key) == 0 || strlen(key) > 15) {
        ESP_LOGE(TAG, "Invalid key length: %lu", (unsigned long)strlen(key));
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Invalid key length\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Open NVS namespace
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Failed to open NVS\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Get key type
    nvs_type_t nvs_type;
    err = nvs_find_key(nvs_handle, key, &nvs_type);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Key not found: %s", key);
        nvs_close(nvs_handle);
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Key not found\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Get session buffer for this connection
    uint8_t *session_buffer = g_working_buffer;
    size_t buffer_size = HTTP_SERVICE_WORKING_BUFFER_SIZE;

    // Use dedicated response buffer
    char *response = (char *)g_response_buffer;
    size_t response_size = HTTP_SERVICE_RESPONSE_BUFFER_SIZE;
    err = ESP_FAIL;

    switch (nvs_type) {
    case NVS_TYPE_ANY:
        ESP_LOGE(TAG, "NVS_TYPE_ANY not supported");
        nvs_close(nvs_handle);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Unsupported NVS type\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    case NVS_TYPE_U8: {
        uint8_t value;
        err = nvs_get_u8(nvs_handle, key, &value);
        if (err == ESP_OK) {
            snprintf(response, response_size, "{\"type\":\"u8\",\"value\":%d}", value);
        }
    } break;
    case NVS_TYPE_I8: {
        int8_t value;
        err = nvs_get_i8(nvs_handle, key, &value);
        if (err == ESP_OK) {
            snprintf(response, response_size, "{\"type\":\"i8\",\"value\":%d}", value);
        }
    } break;
    case NVS_TYPE_U16: {
        uint16_t value;
        err = nvs_get_u16(nvs_handle, key, &value);
        if (err == ESP_OK) {
            snprintf(response, response_size, "{\"type\":\"u16\",\"value\":%d}", value);
        }
    } break;
    case NVS_TYPE_I16: {
        int16_t value;
        err = nvs_get_i16(nvs_handle, key, &value);
        if (err == ESP_OK) {
            snprintf(response, response_size, "{\"type\":\"i16\",\"value\":%d}", value);
        }
    } break;
    case NVS_TYPE_U32: {
        uint32_t value;
        err = nvs_get_u32(nvs_handle, key, &value);
        if (err == ESP_OK) {
            snprintf(response,
                     response_size,
                     "{\"type\":\"u32\",\"value\":%lu}",
                     (unsigned long)value);
        }
    } break;
    case NVS_TYPE_I32: {
        int32_t value;
        err = nvs_get_i32(nvs_handle, key, &value);
        if (err == ESP_OK) {
            snprintf(response,
                     response_size,
                     "{\"type\":\"i32\",\"value\":%ld}",
                     (long)value);
        }
    } break;
    case NVS_TYPE_U64: {
        uint64_t value;
        err = nvs_get_u64(nvs_handle, key, &value);
        if (err == ESP_OK) {
            snprintf(response,
                     response_size,
                     "{\"type\":\"u64\",\"value\":%llu}",
                     (unsigned long long)value);
        }
    } break;
    case NVS_TYPE_I64: {
        int64_t value;
        err = nvs_get_i64(nvs_handle, key, &value);
        if (err == ESP_OK) {
            snprintf(response,
                     response_size,
                     "{\"type\":\"i64\",\"value\":%lld}",
                     (long long)value);
        }
    } break;
    case NVS_TYPE_STR: {
        // Use working buffer for string value
        char *str_value = (char *)g_working_buffer;
        size_t required_size = HTTP_SERVICE_WORKING_BUFFER_SIZE;
        err = nvs_get_str(nvs_handle, key, str_value, &required_size);
        if (err == ESP_OK) {
            int written = snprintf(response,
                                   response_size,
                                   "{\"type\":\"str\",\"value\":\"%s\"}",
                                   str_value);
            if (written >= response_size) {
                ESP_LOGE(TAG, "NVS string value too large for response buffer");
                nvs_close(nvs_handle);
                httpd_resp_set_status(req, "500 Internal Server Error");
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(
                    req, "{\"error\":\"Response too large\"}", HTTPD_RESP_USE_STRLEN);
                return ESP_FAIL;
            }
        }
    } break;
    case NVS_TYPE_BLOB: {
        size_t required_size = buffer_size;
        err = nvs_get_blob(nvs_handle, key, session_buffer, &required_size);
        if (err == ESP_OK) {
            // Return raw binary data
            nvs_close(nvs_handle);
            httpd_resp_set_type(req, "application/octet-stream");
            httpd_resp_send(req, (const char *)session_buffer, required_size);
            return ESP_OK;
        }
    } break;
    }

    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get NVS value: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Failed to get value\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// NVS set handler - POST /api/nvs/{key}
static esp_err_t nvs_set_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "NVS set request received");

    // Check authentication
    if (check_api_key(req) != ESP_OK) {
        return ESP_FAIL;
    }

    // Extract key from URI (skip "/api/nvs/" prefix)
    const char *uri = req->uri;
    const char *key = uri + 9;  // Skip "/api/nvs/"

    if (strlen(key) == 0 || strlen(key) > 15) {
        ESP_LOGE(TAG, "Invalid key length: %lu", (unsigned long)strlen(key));
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Invalid key length\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Get content length
    size_t content_length = req->content_len;
    if (content_length == 0 || content_length > 1024) {
        ESP_LOGE(TAG, "Invalid content length: %lu", (unsigned long)content_length);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Invalid content length\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Use global buffer for JSON body
    if (content_length + 1 > HTTP_SERVICE_WORKING_BUFFER_SIZE) {
        ESP_LOGE(TAG, "JSON body too large: %lu", (unsigned long)content_length);
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Request body too large\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    char *json_body = (char *)g_working_buffer;

    int ret = httpd_req_recv(req, json_body, content_length);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Failed to receive JSON body");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
                        "{\"error\":\"Failed to receive request body\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    json_body[content_length] = '\0';

    // Parse JSON using cJSON
    cJSON *json = cJSON_Parse(json_body);
    if (json == NULL) {
        ESP_LOGE(TAG, "Invalid JSON format");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Invalid JSON format\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Extract type safely
    cJSON *type_json = cJSON_GetObjectItem(json, "type");
    if (type_json == NULL || !cJSON_IsString(type_json)) {
        ESP_LOGE(TAG, "Missing or invalid type field");
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
                        "{\"error\":\"Missing or invalid type field\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    const char *type_str = cJSON_GetStringValue(type_json);

    // Extract value safely
    cJSON *value_json = cJSON_GetObjectItem(json, "value");
    if (value_json == NULL) {
        ESP_LOGE(TAG, "Missing value field");
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Missing value field\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Open NVS namespace
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        cJSON_Delete(json);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Failed to open NVS\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Write value based on type
    err = ESP_FAIL;
    if (strcmp(type_str, "u8") == 0) {
        if (cJSON_IsNumber(value_json)) {
            uint8_t value = (uint8_t)cJSON_GetNumberValue(value_json);
            err = nvs_set_u8(nvs_handle, key, value);
        } else {
            ESP_LOGE(TAG, "Invalid value type for u8");
            err = ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(type_str, "i8") == 0) {
        if (cJSON_IsNumber(value_json)) {
            int8_t value = (int8_t)cJSON_GetNumberValue(value_json);
            err = nvs_set_i8(nvs_handle, key, value);
        } else {
            ESP_LOGE(TAG, "Invalid value type for i8");
            err = ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(type_str, "u16") == 0) {
        if (cJSON_IsNumber(value_json)) {
            uint16_t value = (uint16_t)cJSON_GetNumberValue(value_json);
            err = nvs_set_u16(nvs_handle, key, value);
        } else {
            ESP_LOGE(TAG, "Invalid value type for u16");
            err = ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(type_str, "i16") == 0) {
        if (cJSON_IsNumber(value_json)) {
            int16_t value = (int16_t)cJSON_GetNumberValue(value_json);
            err = nvs_set_i16(nvs_handle, key, value);
        } else {
            ESP_LOGE(TAG, "Invalid value type for i16");
            err = ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(type_str, "u32") == 0) {
        if (cJSON_IsNumber(value_json)) {
            uint32_t value = (uint32_t)cJSON_GetNumberValue(value_json);
            err = nvs_set_u32(nvs_handle, key, value);
        } else {
            ESP_LOGE(TAG, "Invalid value type for u32");
            err = ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(type_str, "i32") == 0) {
        if (cJSON_IsNumber(value_json)) {
            int32_t value = (int32_t)cJSON_GetNumberValue(value_json);
            err = nvs_set_i32(nvs_handle, key, value);
        } else {
            ESP_LOGE(TAG, "Invalid value type for i32");
            err = ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(type_str, "u64") == 0) {
        if (cJSON_IsNumber(value_json)) {
            uint64_t value = (uint64_t)cJSON_GetNumberValue(value_json);
            err = nvs_set_u64(nvs_handle, key, value);
        } else {
            ESP_LOGE(TAG, "Invalid value type for u64");
            err = ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(type_str, "i64") == 0) {
        if (cJSON_IsNumber(value_json)) {
            int64_t value = (int64_t)cJSON_GetNumberValue(value_json);
            err = nvs_set_i64(nvs_handle, key, value);
        } else {
            ESP_LOGE(TAG, "Invalid value type for i64");
            err = ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(type_str, "str") == 0) {
        if (cJSON_IsString(value_json)) {
            const char *value_str = cJSON_GetStringValue(value_json);
            err = nvs_set_str(nvs_handle, key, value_str);
        } else {
            ESP_LOGE(TAG, "Invalid value type for str");
            err = ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(type_str, "blob") == 0) {
        // For blob type, read binary data directly from request body
        // Use global buffer for this connection
        uint8_t *session_buffer = g_working_buffer;

        // Read binary data directly into session buffer
        int ret = httpd_req_recv(req, (char *)session_buffer, content_length);
        if (ret <= 0) {
            ESP_LOGE(TAG, "Failed to receive blob data");
            err = ESP_ERR_INVALID_ARG;
        } else {
            err = nvs_set_blob(nvs_handle, key, session_buffer, ret);
        }
    } else {
        ESP_LOGE(TAG, "Unsupported type: %s", type_str);
        err = ESP_ERR_INVALID_ARG;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set NVS value: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        cJSON_Delete(json);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Failed to set value\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        cJSON_Delete(json);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Failed to commit changes\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    nvs_close(nvs_handle);
    cJSON_Delete(json);

    ESP_LOGI(TAG, "NVS set completed: key='%s'", key);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// NVS delete handler - DELETE /api/nvs/{key}
static esp_err_t nvs_delete_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "NVS delete request received");

    // Check authentication
    if (check_api_key(req) != ESP_OK) {
        return ESP_FAIL;
    }

    // Extract key from URI (skip "/api/nvs/" prefix)
    const char *uri = req->uri;
    const char *key = uri + 9;  // Skip "/api/nvs/"

    if (strlen(key) == 0 || strlen(key) > 15) {
        ESP_LOGE(TAG, "Invalid key length: %lu", (unsigned long)strlen(key));
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Invalid key length\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Open NVS namespace
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Failed to open NVS\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Delete key
    err = nvs_erase_key(nvs_handle, key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase NVS key: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Failed to delete key\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req, "{\"error\":\"Failed to commit changes\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "NVS delete completed: key='%s'", key);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// WiFi event handler for HTTP service
static void http_service_wifi_event_handler(void *arg,
                                            esp_event_base_t event_base,
                                            int32_t event_id,
                                            void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (g_service != NULL) {
            ESP_LOGI(TAG, "WiFi disconnected, stopping HTTP service");
            stop_http_service();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "WiFi connected, starting HTTP service");
        start_http_service();
    }
}

bool http_service_init(void) {
    // Load configuration
    if (!load_http_service_configuration(&g_http_service_config)) {
        ESP_LOGE(TAG, "Failed to load HTTP service configuration");
        return false;
    }
    ESP_LOGI(TAG, "HTTP service configuration loaded");

    // Register event handlers
    esp_err_t ret =
        esp_event_handler_instance_register(WIFI_EVENT,
                                            WIFI_EVENT_STA_DISCONNECTED,
                                            &http_service_wifi_event_handler,
                                            NULL,
                                            &g_wifi_event_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              &http_service_wifi_event_handler,
                                              NULL,
                                              &g_ip_event_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ret));
        esp_event_handler_instance_unregister(
            WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, g_wifi_event_instance);
        return false;
    }

    ESP_LOGI(TAG, "HTTP service event handlers registered");
    return true;
}

uint16_t http_service_get_port(void) {
    return g_http_service_config.service_port;
}

static esp_err_t start_http_service(void) {
    if (g_service != NULL) {
        ESP_LOGW(TAG, "HTTP service already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = g_http_service_config.service_port;
    config.max_uri_handlers = HTTP_SERVICE_MAX_URI_HANDLERS;
    config.max_resp_headers = HTTP_SERVICE_MAX_RESP_HEADERS;
    config.max_open_sockets = HTTP_SERVICE_MAX_OPEN_SOCKETS;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(
        TAG,
        "HTTP server configured for single connection (prevents concurrent uploads)");

    // Start the httpd service
    ESP_LOGI(TAG, "Starting HTTP service on port %d", config.server_port);
    esp_err_t err = httpd_start(&g_service, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP service: %s", esp_err_to_name(err));
        g_service = NULL;
        return err;
    }

    // Flash program management endpoints
    httpd_uri_t program_upload_uri = {.uri = "/api/program/flash",
                                      .method = HTTP_POST,
                                      .handler = flash_program_upload_handler,
                                      .user_ctx = NULL};
    if (httpd_register_uri_handler(g_service, &program_upload_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register flash program upload URI");
        httpd_stop(g_service);
        g_service = NULL;
        return ESP_FAIL;
    }

    httpd_uri_t program_download_uri = {.uri = "/api/program/flash",
                                        .method = HTTP_GET,
                                        .handler = flash_program_download_handler,
                                        .user_ctx = NULL};
    if (httpd_register_uri_handler(g_service, &program_download_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register flash program download URI");
        httpd_stop(g_service);
        g_service = NULL;
        return ESP_FAIL;
    }

    httpd_uri_t program_delete_uri = {.uri = "/api/program/flash",
                                      .method = HTTP_DELETE,
                                      .handler = flash_program_delete_handler,
                                      .user_ctx = NULL};
    if (httpd_register_uri_handler(g_service, &program_delete_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register flash program delete URI");
        httpd_stop(g_service);
        g_service = NULL;
        return ESP_FAIL;
    }

    httpd_uri_t flash_program_execute_uri = {.uri = "/api/program/flash/execute",
                                             .method = HTTP_POST,
                                             .handler = flash_program_execute_handler,
                                             .user_ctx = NULL};
    if (httpd_register_uri_handler(g_service, &flash_program_execute_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register flash program execute URI");
        httpd_stop(g_service);
        g_service = NULL;
        return ESP_FAIL;
    }

    // RAM program management endpoints
    httpd_uri_t ram_program_upload_uri = {.uri = "/api/program/ram",
                                          .method = HTTP_POST,
                                          .handler = ram_program_upload_handler,
                                          .user_ctx = NULL};
    if (httpd_register_uri_handler(g_service, &ram_program_upload_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register RAM program upload URI");
        httpd_stop(g_service);
        g_service = NULL;
        return ESP_FAIL;
    }

    httpd_uri_t ram_program_download_uri = {.uri = "/api/program/ram",
                                            .method = HTTP_GET,
                                            .handler = ram_program_download_handler,
                                            .user_ctx = NULL};
    if (httpd_register_uri_handler(g_service, &ram_program_download_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register RAM program download URI");
        httpd_stop(g_service);
        g_service = NULL;
        return ESP_FAIL;
    }

    httpd_uri_t ram_program_delete_uri = {.uri = "/api/program/ram",
                                          .method = HTTP_DELETE,
                                          .handler = ram_program_delete_handler,
                                          .user_ctx = NULL};
    if (httpd_register_uri_handler(g_service, &ram_program_delete_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register RAM program delete URI");
        httpd_stop(g_service);
        g_service = NULL;
        return ESP_FAIL;
    }

    httpd_uri_t ram_program_execute_uri = {.uri = "/api/program/ram/execute",
                                           .method = HTTP_POST,
                                           .handler = ram_program_execute_handler,
                                           .user_ctx = NULL};
    if (httpd_register_uri_handler(g_service, &ram_program_execute_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register RAM program execute URI");
        httpd_stop(g_service);
        g_service = NULL;
        return ESP_FAIL;
    }

    // NVS management endpoints
    httpd_uri_t nvs_get_uri = {.uri = "/api/nvs/*",
                               .method = HTTP_GET,
                               .handler = nvs_get_handler,
                               .user_ctx = NULL};
    if (httpd_register_uri_handler(g_service, &nvs_get_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register NVS get URI");
        httpd_stop(g_service);
        g_service = NULL;
        return ESP_FAIL;
    }

    httpd_uri_t nvs_set_uri = {.uri = "/api/nvs/*",
                               .method = HTTP_POST,
                               .handler = nvs_set_handler,
                               .user_ctx = NULL};
    if (httpd_register_uri_handler(g_service, &nvs_set_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register NVS set URI");
        httpd_stop(g_service);
        g_service = NULL;
        return ESP_FAIL;
    }

    httpd_uri_t nvs_delete_uri = {.uri = "/api/nvs/*",
                                  .method = HTTP_DELETE,
                                  .handler = nvs_delete_handler,
                                  .user_ctx = NULL};
    if (httpd_register_uri_handler(g_service, &nvs_delete_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register NVS delete URI");
        httpd_stop(g_service);
        g_service = NULL;
        return ESP_FAIL;
    }

    // Add HTTP service to mDNS
    esp_err_t mdns_err = mdns_service_add(
        NULL, "_http", "_tcp", g_http_service_config.service_port, NULL, 0);
    if (mdns_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add mDNS HTTP service: %s", esp_err_to_name(mdns_err));
    }

    ESP_LOGI(TAG, "HTTP service started successfully");
    return ESP_OK;
}

static void stop_http_service(void) {
    if (g_service == NULL) {
        ESP_LOGW(TAG, "HTTP service is not running");
        return;
    }

    // Remove HTTP service from mDNS
    esp_err_t mdns_err = mdns_service_remove("_http", "_tcp");
    if (mdns_err != ESP_OK) {
        ESP_LOGW(
            TAG, "Failed to remove mDNS HTTP service: %s", esp_err_to_name(mdns_err));
    }

    esp_err_t err = httpd_stop(g_service);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop HTTP service: %s", esp_err_to_name(err));
    }
    g_service = NULL;
    ESP_LOGI(TAG, "HTTP service stopped");
}

bool http_service_is_running(void) {
    return (g_service != NULL);
}
