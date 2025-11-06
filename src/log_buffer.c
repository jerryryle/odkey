#include "log_buffer.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "log_buffer";

// Log buffer size: 32KB (now using PSRAM)
#define LOG_BUFFER_SIZE (32 * 1024)

// Ring buffer for storing logs (allocated in PSRAM)
static uint8_t *g_log_buffer = NULL;
static uint32_t g_write_pos = 0;
static uint32_t g_read_pos = 0;
static bool g_buffer_full = false;  // Distinguish between full and empty states
static SemaphoreHandle_t g_buffer_mutex = NULL;

// Global buffer for log formatting
static char g_log_format_buffer[2 * 1024];

// Store the original vprintf function
static vprintf_like_t g_original_vprintf = NULL;

// Custom vprintf handler that writes to both ring buffer and original output
static int log_buffer_vprintf_handler(const char *fmt, va_list args) {
    // Create a copy of va_list for the second use
    va_list args_copy;
    va_copy(args_copy, args);

    // First, call the original vprintf to maintain serial output
    int result = g_original_vprintf(fmt, args);

    // Then write to our ring buffer using the copy
    if (xSemaphoreTake(g_buffer_mutex, portMAX_DELAY) == pdTRUE) {
        // Use global buffer for formatting - no stack allocation
        int len =
            vsnprintf(g_log_format_buffer, sizeof(g_log_format_buffer), fmt, args_copy);

        if (len > 0) {
            // Truncate if too long to fit in our buffer
            if (len >= sizeof(g_log_format_buffer)) {
                len = sizeof(g_log_format_buffer) - 1;
            }

            // Write to ring buffer
            for (int i = 0; i < len; i++) {
                g_log_buffer[g_write_pos] = g_log_format_buffer[i];
                g_write_pos = (g_write_pos + 1) % LOG_BUFFER_SIZE;

                // Check if we've wrapped around (write_pos wrapped back to 0)
                if (g_write_pos == 0 && !g_buffer_full) {
                    g_buffer_full = true;
                }
            }
        }

        xSemaphoreGive(g_buffer_mutex);
    }

    // Clean up the va_list copy
    va_end(args_copy);

    return result;
}

bool log_buffer_init(void) {
    // Create mutex for thread safety
    g_buffer_mutex = xSemaphoreCreateMutex();
    if (g_buffer_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }

    // Allocate log buffer in PSRAM
    g_log_buffer = heap_caps_malloc(LOG_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (g_log_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate log buffer in PSRAM");
        vSemaphoreDelete(g_buffer_mutex);
        g_buffer_mutex = NULL;
        return false;
    }

    // Initialize buffer state
    memset(g_log_buffer, 0, LOG_BUFFER_SIZE);
    g_write_pos = 0;
    g_read_pos = 0;
    g_buffer_full = false;

    // Store the original vprintf and set our custom handler
    g_original_vprintf = esp_log_set_vprintf(log_buffer_vprintf_handler);

    ESP_LOGI(TAG, "Log buffer initialized (%d bytes in PSRAM)", LOG_BUFFER_SIZE);
    return true;
}

uint32_t log_buffer_get_available(void) {
    if (g_buffer_mutex == NULL) {
        return 0;
    }

    uint32_t available = 0;
    if (xSemaphoreTake(g_buffer_mutex, portMAX_DELAY) == pdTRUE) {
        if (g_buffer_full) {
            // Buffer is full, all data is available
            available = LOG_BUFFER_SIZE;
        } else {
            // Buffer not full, available data is from read_pos to write_pos
            available = g_write_pos - g_read_pos;
        }
        xSemaphoreGive(g_buffer_mutex);
    }

    return available;
}

uint32_t log_buffer_read_chunk(uint8_t *buffer, uint32_t max_size) {
    if (buffer == NULL || max_size == 0 || g_buffer_mutex == NULL) {
        return 0;
    }

    uint32_t bytes_read = 0;
    if (xSemaphoreTake(g_buffer_mutex, portMAX_DELAY) == pdTRUE) {
        // Calculate how many bytes we can read from current read position
        uint32_t available = 0;
        if (g_buffer_full) {
            // Buffer is full, all LOG_BUFFER_SIZE bytes are available
            // Calculate how much we can read from current read_pos
            if (g_read_pos == g_write_pos) {
                // Special case: we're at the start of a full buffer, all data available
                available = LOG_BUFFER_SIZE;
            } else if (g_read_pos < g_write_pos) {
                // Normal case: read_pos to write_pos
                available = g_write_pos - g_read_pos;
            } else {
                // Wrapped case: from read_pos to end, then from start to write_pos
                available = LOG_BUFFER_SIZE - g_read_pos + g_write_pos;
            }
        } else {
            // Buffer not full, available data is from read_pos to write_pos
            available = g_write_pos - g_read_pos;
        }

        if (available > 0) {
            uint32_t to_read = (available < max_size) ? available : max_size;

            // Read data from ring buffer
            for (uint32_t i = 0; i < to_read; i++) {
                buffer[i] = g_log_buffer[g_read_pos];
                g_read_pos = (g_read_pos + 1) % LOG_BUFFER_SIZE;
                bytes_read++;
            }
        }

        xSemaphoreGive(g_buffer_mutex);
    }

    return bytes_read;
}

void log_buffer_start_read(void) {
    if (g_buffer_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(g_buffer_mutex, portMAX_DELAY) == pdTRUE) {
        // Reset read pointer to the oldest available data
        if (g_buffer_full) {
            // Buffer has been full, start from write position (oldest data)
            g_read_pos = g_write_pos;
        } else {
            // Buffer never filled, start from beginning
            g_read_pos = 0;
        }

        xSemaphoreGive(g_buffer_mutex);
    }
}

void log_buffer_clear(void) {
    if (g_buffer_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(g_buffer_mutex, portMAX_DELAY) == pdTRUE) {
        g_write_pos = 0;
        g_read_pos = 0;
        g_buffer_full = false;
        if (g_log_buffer != NULL) {
            memset(g_log_buffer, 0, LOG_BUFFER_SIZE);
        }
        xSemaphoreGive(g_buffer_mutex);
    }
}

void log_buffer_deinit(void) {
    if (g_buffer_mutex != NULL) {
        if (xSemaphoreTake(g_buffer_mutex, portMAX_DELAY) == pdTRUE) {
            // Free PSRAM buffer
            if (g_log_buffer != NULL) {
                heap_caps_free(g_log_buffer);
                g_log_buffer = NULL;
            }
            xSemaphoreGive(g_buffer_mutex);

            // Delete mutex
            vSemaphoreDelete(g_buffer_mutex);
            g_buffer_mutex = NULL;
        }
    }
}

void log_serial_printf(const char *fmt, ...) {
    if (g_original_vprintf == NULL) {
        return;  // Buffer not initialized yet
    }

    va_list args;
    va_start(args, fmt);
    g_original_vprintf(fmt, args);
    va_end(args);
}
