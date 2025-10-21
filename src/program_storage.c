#include "program_storage.h"
#include <string.h>
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "spi_flash_mmap.h"

static const char *TAG = "program_storage";

#define PROGRAM_STORAGE_PARTITION_LABEL "odkey_programs"

// Global partition handle
static esp_partition_t const *g_program_partition = NULL;

// mmap for reading
static const uint8_t *g_mmap_data = NULL;
static esp_partition_mmap_handle_t g_mmap_handle = 0;

// Mutex to protect shared state
static SemaphoreHandle_t g_write_state_mutex = NULL;

// Chunked write state
static struct {
    uint32_t expected_size;                     // Expected program size
    uint32_t bytes_written;                     // Total bytes written to flash
    uint32_t buffer_offset;                     // Current position in buffer
    uint8_t buffer[PROGRAM_STORAGE_PAGE_SIZE];  // 4KB page buffer
    program_storage_write_state_t state;        // IDLE, WRITING, ERROR
    program_storage_source_t current_source;    // Current owner of write session
} g_write_state = {0};

// Helper function to convert source enum to string
static const char *source_to_string(program_storage_source_t source) {
    switch (source) {
    case PROGRAM_STORAGE_SOURCE_USB:
        return "USB";
    case PROGRAM_STORAGE_SOURCE_HTTP:
        return "HTTP";
    case PROGRAM_STORAGE_SOURCE_NONE:
    default:
        return "NONE";
    }
}

// Internal function to reset write state (assumes mutex is held)
static void reset_write_state_unsafe(void) {
    g_write_state.bytes_written = 0;
    g_write_state.expected_size = 0;
    g_write_state.buffer_offset = 0;
    memset(g_write_state.buffer, 0, sizeof(g_write_state.buffer));
    g_write_state.state = PROGRAM_STORAGE_STATE_IDLE;
    g_write_state.current_source = PROGRAM_STORAGE_SOURCE_NONE;
}

// Internal function to write a full page to flash (assumes mutex is held)
static bool write_page_to_flash_unsafe(const uint8_t *page_data, size_t page_size) {
    if (g_program_partition == NULL) {
        ESP_LOGE(TAG, "Program storage not initialized");
        return false;
    }

    if (page_data == NULL || page_size != PROGRAM_STORAGE_PAGE_SIZE) {
        ESP_LOGE(TAG,
                 "Page data cannot be NULL and page_size must be exactly %d bytes",
                 PROGRAM_STORAGE_PAGE_SIZE);
        return false;
    }

    if (g_write_state.bytes_written + page_size > g_program_partition->size) {
        ESP_LOGE(TAG, "Page would exceed partition size");
        return false;
    }

    esp_err_t ret = esp_partition_write(
        g_program_partition, g_write_state.bytes_written, page_data, page_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write page: %s", esp_err_to_name(ret));
        return false;
    }

    g_write_state.bytes_written += page_size;
    ESP_LOGD(TAG,
             "Wrote page: %lu bytes (total: %lu)",
             (unsigned long)page_size,
             (unsigned long)g_write_state.bytes_written);

    return true;
}

bool program_storage_init(void) {
    // Create mutex to protect shared state
    g_write_state_mutex = xSemaphoreCreateMutex();
    if (g_write_state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create write state mutex");
        return false;
    }

    // Find the program storage partition
    g_program_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                   ESP_PARTITION_SUBTYPE_DATA_UNDEFINED,
                                                   PROGRAM_STORAGE_PARTITION_LABEL);

    if (g_program_partition == NULL) {
        ESP_LOGE(TAG,
                 "Failed to find program storage partition: %s",
                 PROGRAM_STORAGE_PARTITION_LABEL);
        vSemaphoreDelete(g_write_state_mutex);
        g_write_state_mutex = NULL;
        return false;
    }

    ESP_LOGI(TAG,
             "Found program storage partition: %s (size: %lu bytes)",
             g_program_partition->label,
             (unsigned long)g_program_partition->size);

    // Create mmap for reading
    esp_err_t ret = esp_partition_mmap(g_program_partition,
                                       0,
                                       g_program_partition->size,
                                       ESP_PARTITION_MMAP_DATA,
                                       (const void **)&g_mmap_data,
                                       &g_mmap_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create mmap: %s", esp_err_to_name(ret));
        vSemaphoreDelete(g_write_state_mutex);
        g_write_state_mutex = NULL;
        return false;
    }

    ESP_LOGI(TAG, "Created mmap for program storage");

    return true;
}

const uint8_t *program_storage_get(uint32_t *out_size) {
    if (g_program_partition == NULL || g_mmap_data == NULL) {
        ESP_LOGE(TAG, "Program storage not initialized");
        if (out_size)
            *out_size = 0;
        return NULL;
    }

    if (out_size == NULL) {
        ESP_LOGE(TAG, "out_size parameter cannot be NULL");
        return NULL;
    }

    // Use mmap data
    const uint8_t *data = g_mmap_data;

    // Read program size from first 4 bytes
    uint32_t program_size = 0;
    memcpy(&program_size, data, sizeof(program_size));

    // Check if partition is empty (all zeros) or has invalid size
    if ((program_size == 0) || (program_size > PROGRAM_STORAGE_MAX_SIZE)) {
        ESP_LOGD(TAG,
                 "No valid program in storage (size: %lu)",
                 (unsigned long)program_size);
        *out_size = 0;
        return NULL;
    }

    ESP_LOGI(TAG, "Found program in storage: %lu bytes", (unsigned long)program_size);
    *out_size = program_size;

    // Return pointer to program data (skip the size header page)
    return data + PROGRAM_STORAGE_PAGE_SIZE;
}

static bool program_storage_write_start_unsafe(uint32_t expected_program_size,
                                               program_storage_source_t source) {
    // Validate expected program size
    if (expected_program_size == 0) {
        ESP_LOGE(TAG, "Expected program size cannot be zero");
        return false;
    }

    if (expected_program_size > PROGRAM_STORAGE_MAX_SIZE) {
        ESP_LOGE(TAG,
                 "Expected program size too large: %lu bytes (max: %lu)",
                 (unsigned long)expected_program_size,
                 (unsigned long)(PROGRAM_STORAGE_MAX_SIZE));
        return false;
    }

    // Calculate sectors needed (round up to 4KB boundaries)
    // ESP32 flash sectors are 4KB (0x1000 bytes)
    // We need to erase: reserved first page + program data
    uint32_t total_size_needed = PROGRAM_STORAGE_PAGE_SIZE + expected_program_size;
    uint32_t sectors_needed = (total_size_needed + (PROGRAM_STORAGE_PAGE_SIZE - 1)) /
                              PROGRAM_STORAGE_PAGE_SIZE;
    uint32_t erase_size = sectors_needed * PROGRAM_STORAGE_PAGE_SIZE;

    // Check if we're interrupting an existing write session
    if (g_write_state.state == PROGRAM_STORAGE_STATE_WRITING &&
        g_write_state.current_source != source) {
        ESP_LOGI(TAG,
                 "Write session interrupted by %s (was: %s)",
                 source_to_string(source),
                 source_to_string(g_write_state.current_source));
    }

    ESP_LOGI(TAG,
             "Starting chunked write for %s (program: %lu bytes, erasing: %lu bytes, "
             "sectors: %lu)",
             source_to_string(source),
             (unsigned long)expected_program_size,
             (unsigned long)erase_size,
             (unsigned long)sectors_needed);

    // Erase only the necessary sectors
    esp_err_t ret = esp_partition_erase_range(g_program_partition, 0, erase_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase partition range: %s", esp_err_to_name(ret));
        return false;
    }

    // Initialize write state
    g_write_state.bytes_written =
        PROGRAM_STORAGE_PAGE_SIZE;  // Skip over the entire first page (reserved for
                                    // size header)
    g_write_state.expected_size = expected_program_size;
    g_write_state.buffer_offset = 0;
    memset(g_write_state.buffer, 0, sizeof(g_write_state.buffer));
    g_write_state.state = PROGRAM_STORAGE_STATE_WRITING;
    g_write_state.current_source = source;

    return true;
}

bool program_storage_write_start(uint32_t expected_program_size,
                                 program_storage_source_t source) {
    if (g_program_partition == NULL || g_write_state_mutex == NULL) {
        ESP_LOGE(TAG, "Program storage not initialized");
        return false;
    }

    // Lock mutex to protect shared state
    if (xSemaphoreTake(g_write_state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take write state mutex");
        return false;
    }

    bool result = program_storage_write_start_unsafe(expected_program_size, source);
    xSemaphoreGive(g_write_state_mutex);
    return result;
}

static bool program_storage_write_chunk_unsafe(const uint8_t *data,
                                               uint32_t size,
                                               program_storage_source_t source) {
    if (g_write_state.state != PROGRAM_STORAGE_STATE_WRITING) {
        ESP_LOGE(
            TAG,
            "Program storage write chunk called but not in writing state (state: %d)",
            g_write_state.state);
        return false;
    }

    if (g_write_state.current_source != source) {
        ESP_LOGE(TAG,
                 "Source mismatch: expected %s, got %s. %s may have interrupted the "
                 "old write session with a new one",
                 source_to_string(g_write_state.current_source),
                 source_to_string(source),
                 source_to_string(g_write_state.current_source));
        return false;
    }

    if (data == NULL || size == 0) {
        ESP_LOGE(TAG,
                 "Invalid write parameters: data=%p, size=%lu",
                 data,
                 (unsigned long)size);
        g_write_state.state = PROGRAM_STORAGE_STATE_ERROR;
        return false;
    }

    if (size > PROGRAM_STORAGE_PAGE_SIZE) {
        ESP_LOGE(TAG,
                 "Write size too large: %lu bytes (max: %d)",
                 (unsigned long)size,
                 PROGRAM_STORAGE_PAGE_SIZE);
        g_write_state.state = PROGRAM_STORAGE_STATE_ERROR;
        return false;
    }

    // Check if we would exceed expected total size
    size_t program_bytes_written =
        g_write_state.bytes_written -
        PROGRAM_STORAGE_PAGE_SIZE;  // Subtract reserved first page
    if (program_bytes_written + size > g_write_state.expected_size) {
        ESP_LOGE(TAG,
                 "Write would exceed expected total size: %lu + %lu > %lu",
                 (unsigned long)program_bytes_written,
                 (unsigned long)size,
                 (unsigned long)g_write_state.expected_size);
        g_write_state.state = PROGRAM_STORAGE_STATE_ERROR;
        return false;
    }

    uint32_t bytes_remaining = size;
    const uint8_t *data_ptr = data;

    while (bytes_remaining > 0) {
        // Calculate how many bytes we can copy to the buffer
        uint32_t buffer_space = PROGRAM_STORAGE_PAGE_SIZE - g_write_state.buffer_offset;
        uint32_t bytes_to_copy =
            (bytes_remaining < buffer_space) ? bytes_remaining : buffer_space;

        // Copy data to buffer
        memcpy(g_write_state.buffer + g_write_state.buffer_offset,
               data_ptr,
               bytes_to_copy);
        g_write_state.buffer_offset += bytes_to_copy;

        // Update remaining bytes and pointer
        bytes_remaining -= bytes_to_copy;
        data_ptr += bytes_to_copy;

        // If buffer is full, write it out
        if (g_write_state.buffer_offset >= PROGRAM_STORAGE_PAGE_SIZE) {
            if (!write_page_to_flash_unsafe(g_write_state.buffer,
                                            PROGRAM_STORAGE_PAGE_SIZE)) {
                ESP_LOGE(TAG, "Failed to write page to flash");
                g_write_state.state = PROGRAM_STORAGE_STATE_ERROR;
                return false;
            }

            // Reset buffer for next page
            memset(g_write_state.buffer, 0, sizeof(g_write_state.buffer));
            g_write_state.buffer_offset = 0;
        }
    }

    ESP_LOGD(TAG,
             "Buffered %lu bytes, total written: %lu/%lu",
             (unsigned long)size,
             (unsigned long)(g_write_state.bytes_written - PROGRAM_STORAGE_PAGE_SIZE),
             (unsigned long)g_write_state.expected_size);

    return true;
}

bool program_storage_write_chunk(const uint8_t *data,
                                 uint32_t size,
                                 program_storage_source_t source) {
    if (g_program_partition == NULL || g_write_state_mutex == NULL) {
        ESP_LOGE(TAG, "Program storage not initialized");
        return false;
    }

    // Lock mutex to protect shared state
    if (xSemaphoreTake(g_write_state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take write state mutex");
        return false;
    }

    bool result = program_storage_write_chunk_unsafe(data, size, source);
    xSemaphoreGive(g_write_state_mutex);
    return result;
}

static bool program_storage_write_finish_unsafe(uint32_t program_size,
                                                program_storage_source_t source) {
    if (g_write_state.state != PROGRAM_STORAGE_STATE_WRITING) {
        ESP_LOGE(
            TAG,
            "Program storage write finish called but not in writing state (state: %d)",
            g_write_state.state);
        return false;
    }

    if (g_write_state.current_source != source) {
        ESP_LOGE(TAG,
                 "Source mismatch: expected %s, got %s",
                 source_to_string(g_write_state.current_source),
                 source_to_string(source));
        return false;
    }

    if (program_size == 0) {
        ESP_LOGE(TAG, "Program size cannot be zero");
        g_write_state.state = PROGRAM_STORAGE_STATE_ERROR;
        return false;
    }

    if (program_size > PROGRAM_STORAGE_MAX_SIZE) {
        ESP_LOGE(TAG,
                 "Program too large: %lu bytes (max: %lu)",
                 (unsigned long)program_size,
                 (unsigned long)(PROGRAM_STORAGE_MAX_SIZE));
        g_write_state.state = PROGRAM_STORAGE_STATE_ERROR;
        return false;
    }

    // If there's remaining data in the buffer, write it as a final page
    if (g_write_state.buffer_offset > 0) {
        if (!write_page_to_flash_unsafe(g_write_state.buffer,
                                        PROGRAM_STORAGE_PAGE_SIZE)) {
            ESP_LOGE(TAG, "Failed to write final page to flash");
            g_write_state.state = PROGRAM_STORAGE_STATE_ERROR;
            return false;
        }
    }

    // Validate that we've written at least as many flash bytes as expected for the
    // program (accounting for page alignment - we may have written more due to padding)
    uint32_t program_bytes_written =
        g_write_state.bytes_written -
        PROGRAM_STORAGE_PAGE_SIZE;  // Reserved page + program data
    if (program_bytes_written < program_size) {
        ESP_LOGE(TAG,
                 "Insufficient program data written: %lu bytes written, expected at "
                 "least %lu",
                 (unsigned long)program_bytes_written,
                 (unsigned long)program_size);
        g_write_state.state = PROGRAM_STORAGE_STATE_ERROR;
        return false;
    }

    // Write the size header at offset 0 (this makes the program "valid")
    // Use uint32_t for consistent cross-platform compatibility
    uint32_t size_header = program_size;
    esp_err_t ret =
        esp_partition_write(g_program_partition, 0, &size_header, sizeof(size_header));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write size header: %s", esp_err_to_name(ret));
        g_write_state.state = PROGRAM_STORAGE_STATE_ERROR;
        return false;
    }

    ESP_LOGI(TAG,
             "Successfully completed chunked write: %lu bytes",
             (unsigned long)program_size);

    // Reset state
    reset_write_state_unsafe();
    return true;
}

bool program_storage_write_finish(uint32_t program_size,
                                  program_storage_source_t source) {
    if (g_write_state_mutex == NULL) {
        ESP_LOGE(TAG, "Program storage not initialized");
        return false;
    }

    // Lock mutex to protect shared state
    if (xSemaphoreTake(g_write_state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take write state mutex");
        return false;
    }

    bool result = program_storage_write_finish_unsafe(program_size, source);
    xSemaphoreGive(g_write_state_mutex);
    return result;
}

bool program_storage_erase(void) {
    if (g_program_partition == NULL) {
        ESP_LOGE(TAG, "Program storage not initialized");
        return false;
    }

    ESP_LOGI(TAG, "Erasing program from storage");

    // Erase the entire partition
    esp_err_t ret =
        esp_partition_erase_range(g_program_partition, 0, g_program_partition->size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase partition: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "Successfully erased program from storage");
    return true;
}

program_storage_write_state_t program_storage_get_write_state(void) {
    if (g_write_state_mutex == NULL) {
        return PROGRAM_STORAGE_STATE_ERROR;
    }

    if (xSemaphoreTake(g_write_state_mutex, portMAX_DELAY) != pdTRUE) {
        return PROGRAM_STORAGE_STATE_ERROR;
    }

    program_storage_write_state_t state = g_write_state.state;
    xSemaphoreGive(g_write_state_mutex);
    return state;
}

uint32_t program_storage_get_bytes_written(void) {
    if (g_write_state_mutex == NULL) {
        return 0;
    }

    if (xSemaphoreTake(g_write_state_mutex, portMAX_DELAY) != pdTRUE) {
        return 0;
    }

    // Return only the program bytes written (excluding the reserved first page)
    uint32_t bytes_written = 0;
    if (g_write_state.bytes_written > PROGRAM_STORAGE_PAGE_SIZE) {
        bytes_written = g_write_state.bytes_written - PROGRAM_STORAGE_PAGE_SIZE;
    }

    xSemaphoreGive(g_write_state_mutex);
    return bytes_written;
}

uint32_t program_storage_get_expected_size(void) {
    if (g_write_state_mutex == NULL) {
        return 0;
    }

    if (xSemaphoreTake(g_write_state_mutex, portMAX_DELAY) != pdTRUE) {
        return 0;
    }

    uint32_t expected_size = g_write_state.expected_size;
    xSemaphoreGive(g_write_state_mutex);
    return expected_size;
}
