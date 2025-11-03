#include "program_ram.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "program_ram";

/**
 * @brief Program storage write state (private to this module)
 */
typedef enum {
    PROGRAM_STORAGE_STATE_IDLE,
    PROGRAM_STORAGE_STATE_WRITING,
    PROGRAM_STORAGE_STATE_ERROR
} program_storage_write_state_t;

// Mutex to protect RAM state
static SemaphoreHandle_t g_ram_write_state_mutex = NULL;

// RAM write state
static struct {
    uint32_t expected_size;                 // Expected program size
    uint32_t bytes_written;                 // Total bytes written to RAM buffer
    uint32_t buffer_offset;                 // Current position in write buffer
    uint8_t *buffer;                        // 1MB write buffer (allocated in PSRAM)
    program_storage_write_state_t state;    // IDLE, WRITING, ERROR
    program_write_source_t current_source;  // Current owner of write session
    uint32_t stored_program_size;           // Stored program size (set in finish)
} g_ram_write_state = {0};

// Helper function to convert source enum to string
static const char *source_to_string(program_write_source_t source) {
    switch (source) {
    case PROGRAM_WRITE_SOURCE_USB:
        return "USB";
    case PROGRAM_WRITE_SOURCE_HTTP:
        return "HTTP";
    case PROGRAM_WRITE_SOURCE_NONE:
    default:
        return "NONE";
    }
}

// Internal function to reset RAM write state (assumes mutex is held)
static void reset_ram_write_state_unsafe(void) {
    g_ram_write_state.bytes_written = 0;
    g_ram_write_state.expected_size = 0;
    g_ram_write_state.buffer_offset = 0;
    if (g_ram_write_state.buffer != NULL) {
        memset(g_ram_write_state.buffer, 0, PROGRAM_RAM_MAX_SIZE);
    }
    g_ram_write_state.state = PROGRAM_STORAGE_STATE_IDLE;
    g_ram_write_state.current_source = PROGRAM_WRITE_SOURCE_NONE;
    g_ram_write_state.stored_program_size = 0;
}

bool program_ram_init(void) {
    // Create mutex to protect RAM state
    g_ram_write_state_mutex = xSemaphoreCreateMutex();
    if (g_ram_write_state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create RAM write state mutex");
        return false;
    }

    // Allocate program buffer in PSRAM
    g_ram_write_state.buffer =
        heap_caps_malloc(PROGRAM_RAM_MAX_SIZE, MALLOC_CAP_SPIRAM);
    if (g_ram_write_state.buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate program buffer in PSRAM");
        vSemaphoreDelete(g_ram_write_state_mutex);
        g_ram_write_state_mutex = NULL;
        return false;
    }

    // Initialize buffer state
    memset(g_ram_write_state.buffer, 0, PROGRAM_RAM_MAX_SIZE);
    g_ram_write_state.bytes_written = 0;
    g_ram_write_state.expected_size = 0;
    g_ram_write_state.buffer_offset = 0;
    g_ram_write_state.state = PROGRAM_STORAGE_STATE_IDLE;
    g_ram_write_state.current_source = PROGRAM_WRITE_SOURCE_NONE;
    g_ram_write_state.stored_program_size = 0;

    ESP_LOGI(TAG, "RAM storage initialized (%d bytes in PSRAM)", PROGRAM_RAM_MAX_SIZE);
    return true;
}

// Internal function to get RAM program data (assumes mutex is held)
static const uint8_t *program_ram_get_unsafe(uint32_t *out_size) {
    if (out_size == NULL) {
        ESP_LOGE(TAG, "out_size parameter cannot be NULL");
        return NULL;
    }

    // Check if we're currently in a write operation
    if (g_ram_write_state.state != PROGRAM_STORAGE_STATE_IDLE) {
        ESP_LOGD(
            TAG,
            "Cannot get RAM program while write operation is in progress (state: %d)",
            g_ram_write_state.state);
        *out_size = 0;
        return NULL;
    }

    // Check if we have a valid program
    if (g_ram_write_state.stored_program_size == 0) {
        ESP_LOGD(TAG, "No valid RAM program in storage");
        *out_size = 0;
        return NULL;
    }

    ESP_LOGI(TAG,
             "Found RAM program in storage: %lu bytes",
             (unsigned long)g_ram_write_state.stored_program_size);
    *out_size = g_ram_write_state.stored_program_size;

    // Return pointer to RAM program buffer
    return g_ram_write_state.buffer;
}

const uint8_t *program_ram_get(uint32_t *out_size) {
    if (g_ram_write_state_mutex == NULL) {
        ESP_LOGE(TAG, "RAM storage not initialized");
        if (out_size)
            *out_size = 0;
        return NULL;
    }

    // Lock mutex to protect RAM state
    if (xSemaphoreTake(g_ram_write_state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take RAM write state mutex");
        if (out_size)
            *out_size = 0;
        return NULL;
    }

    const uint8_t *result = program_ram_get_unsafe(out_size);
    xSemaphoreGive(g_ram_write_state_mutex);
    return result;
}

static bool program_ram_write_start_unsafe(uint32_t expected_program_size,
                                           program_write_source_t source) {
    // Validate expected program size
    if (expected_program_size == 0) {
        ESP_LOGE(TAG, "Expected RAM program size cannot be zero");
        return false;
    }

    if (expected_program_size > PROGRAM_RAM_MAX_SIZE) {
        ESP_LOGE(TAG,
                 "Expected RAM program size too large: %lu bytes (max: %lu)",
                 (unsigned long)expected_program_size,
                 (unsigned long)PROGRAM_RAM_MAX_SIZE);
        return false;
    }

    // Check if we're interrupting an existing write session
    if (g_ram_write_state.state == PROGRAM_STORAGE_STATE_WRITING &&
        g_ram_write_state.current_source != source) {
        ESP_LOGI(TAG,
                 "RAM write session interrupted by %s (was: %s)",
                 source_to_string(source),
                 source_to_string(g_ram_write_state.current_source));
    }

    ESP_LOGI(TAG,
             "Starting RAM write for %s (program: %lu bytes)",
             source_to_string(source),
             (unsigned long)expected_program_size);

    // Initialize RAM write state
    g_ram_write_state.expected_size = expected_program_size;
    g_ram_write_state.bytes_written = 0;
    g_ram_write_state.buffer_offset = 0;
    if (g_ram_write_state.buffer != NULL) {
        memset(g_ram_write_state.buffer, 0, PROGRAM_RAM_MAX_SIZE);
    }
    g_ram_write_state.state = PROGRAM_STORAGE_STATE_WRITING;
    g_ram_write_state.current_source = source;
    g_ram_write_state.stored_program_size = 0;

    return true;
}

bool program_ram_write_start(uint32_t expected_program_size,
                             program_write_source_t source) {
    if (g_ram_write_state_mutex == NULL) {
        ESP_LOGE(TAG, "RAM storage not initialized");
        return false;
    }

    // Lock mutex to protect RAM state
    if (xSemaphoreTake(g_ram_write_state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take RAM write state mutex");
        return false;
    }

    bool result = program_ram_write_start_unsafe(expected_program_size, source);
    xSemaphoreGive(g_ram_write_state_mutex);
    return result;
}

static bool program_ram_write_chunk_unsafe(const uint8_t *data,
                                           uint32_t size,
                                           program_write_source_t source) {
    if (g_ram_write_state.state != PROGRAM_STORAGE_STATE_WRITING) {
        ESP_LOGE(TAG,
                 "RAM program storage write chunk called but not in writing state "
                 "(state: %d)",
                 g_ram_write_state.state);
        return false;
    }

    if (g_ram_write_state.current_source != source) {
        ESP_LOGE(TAG,
                 "Source mismatch: expected %s, got %s",
                 source_to_string(g_ram_write_state.current_source),
                 source_to_string(source));
        return false;
    }

    if (data == NULL || size == 0) {
        ESP_LOGE(TAG,
                 "Invalid write parameters: data=%p, size=%lu",
                 data,
                 (unsigned long)size);
        g_ram_write_state.state = PROGRAM_STORAGE_STATE_ERROR;
        return false;
    }

    if (size > PROGRAM_RAM_MAX_SIZE) {
        ESP_LOGE(TAG,
                 "Write size too large: %lu bytes (max: %lu)",
                 (unsigned long)size,
                 (unsigned long)PROGRAM_RAM_MAX_SIZE);
        g_ram_write_state.state = PROGRAM_STORAGE_STATE_ERROR;
        return false;
    }

    // Check if we would exceed expected total size
    if (g_ram_write_state.bytes_written + size > g_ram_write_state.expected_size) {
        ESP_LOGE(TAG,
                 "Write would exceed expected total size: %lu + %lu > %lu",
                 (unsigned long)g_ram_write_state.bytes_written,
                 (unsigned long)size,
                 (unsigned long)g_ram_write_state.expected_size);
        g_ram_write_state.state = PROGRAM_STORAGE_STATE_ERROR;
        return false;
    }

    // Check if we would exceed buffer size
    if (g_ram_write_state.buffer_offset + size > PROGRAM_RAM_MAX_SIZE) {
        ESP_LOGE(TAG,
                 "Write would exceed buffer size: %lu + %lu > %lu",
                 (unsigned long)g_ram_write_state.buffer_offset,
                 (unsigned long)size,
                 (unsigned long)PROGRAM_RAM_MAX_SIZE);
        g_ram_write_state.state = PROGRAM_STORAGE_STATE_ERROR;
        return false;
    }

    // Simple memcpy to RAM buffer (no page alignment needed)
    memcpy(g_ram_write_state.buffer + g_ram_write_state.buffer_offset, data, size);
    g_ram_write_state.buffer_offset += size;
    g_ram_write_state.bytes_written += size;

    ESP_LOGD(TAG,
             "Buffered %lu bytes to RAM, total written: %lu/%lu",
             (unsigned long)size,
             (unsigned long)g_ram_write_state.bytes_written,
             (unsigned long)g_ram_write_state.expected_size);

    return true;
}

bool program_ram_write_chunk(const uint8_t *data,
                             uint32_t size,
                             program_write_source_t source) {
    if (g_ram_write_state_mutex == NULL) {
        ESP_LOGE(TAG, "RAM storage not initialized");
        return false;
    }

    // Lock mutex to protect RAM state
    if (xSemaphoreTake(g_ram_write_state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take RAM write state mutex");
        return false;
    }

    bool result = program_ram_write_chunk_unsafe(data, size, source);
    xSemaphoreGive(g_ram_write_state_mutex);
    return result;
}

static bool program_ram_write_finish_unsafe(uint32_t program_size,
                                            program_write_source_t source) {
    if (g_ram_write_state.state != PROGRAM_STORAGE_STATE_WRITING) {
        ESP_LOGE(TAG,
                 "RAM program storage write finish called but not in writing state "
                 "(state: %d)",
                 g_ram_write_state.state);
        return false;
    }

    if (g_ram_write_state.current_source != source) {
        ESP_LOGE(TAG,
                 "Source mismatch: expected %s, got %s",
                 source_to_string(g_ram_write_state.current_source),
                 source_to_string(source));
        return false;
    }

    if (program_size == 0) {
        ESP_LOGE(TAG, "RAM program size cannot be zero");
        g_ram_write_state.state = PROGRAM_STORAGE_STATE_ERROR;
        return false;
    }

    if (program_size > PROGRAM_RAM_MAX_SIZE) {
        ESP_LOGE(TAG,
                 "RAM program too large: %lu bytes (max: %lu)",
                 (unsigned long)program_size,
                 (unsigned long)PROGRAM_RAM_MAX_SIZE);
        g_ram_write_state.state = PROGRAM_STORAGE_STATE_ERROR;
        return false;
    }

    // Validate that we've written at least the expected amount, it could be more due to
    // chunk padding
    if (g_ram_write_state.bytes_written < program_size) {
        ESP_LOGE(TAG,
                 "RAM program size mismatch: written %lu, expected at least %lu",
                 (unsigned long)g_ram_write_state.bytes_written,
                 (unsigned long)program_size);
        g_ram_write_state.state = PROGRAM_STORAGE_STATE_ERROR;
        return false;
    }

    // Program is already in the buffer, just mark it as stored
    g_ram_write_state.stored_program_size = program_size;
    g_ram_write_state.state = PROGRAM_STORAGE_STATE_IDLE;
    g_ram_write_state.current_source = PROGRAM_WRITE_SOURCE_NONE;

    ESP_LOGI(TAG,
             "Successfully completed RAM write: %lu bytes",
             (unsigned long)program_size);

    return true;
}

bool program_ram_write_finish(uint32_t program_size, program_write_source_t source) {
    if (g_ram_write_state_mutex == NULL) {
        ESP_LOGE(TAG, "RAM storage not initialized");
        return false;
    }

    // Lock mutex to protect RAM state
    if (xSemaphoreTake(g_ram_write_state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take RAM write state mutex");
        return false;
    }

    bool result = program_ram_write_finish_unsafe(program_size, source);
    xSemaphoreGive(g_ram_write_state_mutex);
    return result;
}

bool program_ram_erase(void) {
    ESP_LOGI(TAG, "Erasing program from RAM storage");

    if (g_ram_write_state_mutex != NULL) {
        if (xSemaphoreTake(g_ram_write_state_mutex, portMAX_DELAY) == pdTRUE) {
            // Zero the RAM buffer and reset state
            reset_ram_write_state_unsafe();
            xSemaphoreGive(g_ram_write_state_mutex);
        }
    }

    ESP_LOGI(TAG, "Successfully erased program from RAM storage");
    return true;
}

uint32_t program_ram_get_bytes_written(void) {
    if (g_ram_write_state_mutex == NULL) {
        return 0;
    }

    if (xSemaphoreTake(g_ram_write_state_mutex, portMAX_DELAY) != pdTRUE) {
        return 0;
    }

    uint32_t ram_bytes_written = g_ram_write_state.bytes_written;
    xSemaphoreGive(g_ram_write_state_mutex);
    return ram_bytes_written;
}

uint32_t program_ram_get_expected_size(void) {
    if (g_ram_write_state_mutex == NULL) {
        return 0;
    }

    if (xSemaphoreTake(g_ram_write_state_mutex, portMAX_DELAY) != pdTRUE) {
        return 0;
    }

    uint32_t ram_expected_size = g_ram_write_state.expected_size;
    xSemaphoreGive(g_ram_write_state_mutex);
    return ram_expected_size;
}
