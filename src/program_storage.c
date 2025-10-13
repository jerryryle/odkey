#include "program_storage.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_flash.h"
#include "spi_flash_mmap.h"
#include <string.h>

static const char *TAG = "program_storage";

#define PROGRAM_STORAGE_PARTITION_LABEL "odkey_programs"

// Global partition handle
static esp_partition_t const *g_program_partition = NULL;

// Chunked write state
static struct {
    size_t bytes_written;
} g_write_state = {0};

bool program_storage_init(void) {
    // Find the program storage partition
    g_program_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 
                                                   ESP_PARTITION_SUBTYPE_DATA_UNDEFINED, 
                                                   PROGRAM_STORAGE_PARTITION_LABEL);
    
    if (g_program_partition == NULL) {
        ESP_LOGE(TAG, "Failed to find program storage partition: %s", PROGRAM_STORAGE_PARTITION_LABEL);
        return false;
    }
    
    ESP_LOGI(TAG, "Found program storage partition: %s (size: %lu bytes)", 
             g_program_partition->label, (unsigned long)g_program_partition->size);
    
    return true;
}

const uint8_t* program_storage_get(size_t* out_size) {
    if (g_program_partition == NULL) {
        ESP_LOGE(TAG, "Program storage not initialized");
        if (out_size) *out_size = 0;
        return NULL;
    }
    
    if (out_size == NULL) {
        ESP_LOGE(TAG, "out_size parameter cannot be NULL");
        return NULL;
    }
    
    // Map the partition to memory for reading
    const void* partition_data;
    esp_partition_mmap_handle_t mmap_handle;
    
    esp_err_t ret = esp_partition_mmap(g_program_partition, 0, g_program_partition->size,
                                       ESP_PARTITION_MMAP_DATA, &partition_data, &mmap_handle);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mmap partition: %s", esp_err_to_name(ret));
        *out_size = 0;
        return NULL;
    }
    
    const uint8_t* data = (const uint8_t*)partition_data;
    
    // Read program size from first 4 bytes
    uint32_t program_size = 0;
    memcpy(&program_size, data, sizeof(program_size));
    
    // Check if partition is empty (all zeros) or has invalid size
    if (program_size == 0 || program_size > PROGRAM_STORAGE_MAX_SIZE - sizeof(program_size)) {
        ESP_LOGD(TAG, "No valid program in storage (size: %lu)", (unsigned long)program_size);
        esp_partition_munmap(mmap_handle);
        *out_size = 0;
        return NULL;
    }
    
    ESP_LOGI(TAG, "Found program in storage: %lu bytes", (unsigned long)program_size);
    *out_size = program_size;
    
    // Return pointer to program data (skip the 4-byte size header)
    return data + sizeof(program_size);
}

bool program_storage_write_start(void) {
    if (g_program_partition == NULL) {
        ESP_LOGE(TAG, "Program storage not initialized");
        return false;
    }

    ESP_LOGI(TAG, "Starting chunked write");

    // Erase the partition first
    esp_err_t ret = esp_partition_erase_range(g_program_partition, 0, g_program_partition->size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase partition: %s", esp_err_to_name(ret));
        return false;
    }

    // Initialize write state
    g_write_state.bytes_written = sizeof(uint32_t); // Skip over the program size header

    return true;
}

bool program_storage_write_chunk(const uint8_t* chunk, size_t chunk_size) {
    if (g_program_partition == NULL) {
        ESP_LOGE(TAG, "Program storage not initialized");
        return false;
    }
    
    if (chunk == NULL || chunk_size == 0 || chunk_size % 4 != 0) {
        ESP_LOGE(TAG, "Chunk data cannot be NULL, zero, or not multiple of 4 bytes");
        return false;
    }
    
    if (g_write_state.bytes_written + chunk_size > PROGRAM_STORAGE_MAX_SIZE) {
        ESP_LOGE(TAG, "Chunk would exceed partition size");
        return false;
    }
    
    esp_err_t ret = esp_partition_write(g_program_partition, g_write_state.bytes_written, chunk, chunk_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write chunk: %s", esp_err_to_name(ret));
        return false;
    }
    
    g_write_state.bytes_written += chunk_size;
    ESP_LOGD(TAG, "Wrote chunk: %lu bytes (total: %lu)",
             (unsigned long)chunk_size, (unsigned long)g_write_state.bytes_written);

    return true;
}

bool program_storage_write_finish(size_t program_size) {
    if (g_program_partition == NULL) {
        ESP_LOGE(TAG, "Program storage not initialized");
        return false;
    }

    if (program_size == 0) {
        ESP_LOGE(TAG, "Program size cannot be zero");
        return false;
    }

    if (program_size > PROGRAM_STORAGE_MAX_SIZE - sizeof(uint32_t)) {
        ESP_LOGE(TAG, "Program too large: %lu bytes (max: %lu)",
                 (unsigned long)program_size, (unsigned long)(PROGRAM_STORAGE_MAX_SIZE - sizeof(uint32_t)));
        return false;
    }

    // Check if we wrote at least the expected amount of program data
    size_t actual_program_bytes = g_write_state.bytes_written - sizeof(uint32_t);
    if (actual_program_bytes < program_size) {
        ESP_LOGE(TAG, "Write incomplete: wrote %lu bytes, expected at least %lu",
                 (unsigned long)actual_program_bytes, (unsigned long)program_size);
        return false;
    }

    // Write the size header at offset 0 (this makes the program "valid")
    uint32_t program_size_header = (uint32_t)program_size;
    esp_err_t ret = esp_partition_write(g_program_partition, 0, &program_size_header, sizeof(program_size_header));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write size header: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "Successfully completed chunked write: %lu bytes", (unsigned long)program_size);
    return true;
}

bool program_storage_erase(void) {
    if (g_program_partition == NULL) {
        ESP_LOGE(TAG, "Program storage not initialized");
        return false;
    }
    
    ESP_LOGI(TAG, "Erasing program from storage");
    
    // Erase the entire partition
    esp_err_t ret = esp_partition_erase_range(g_program_partition, 0, g_program_partition->size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase partition: %s", esp_err_to_name(ret));
        return false;
    }
    
    ESP_LOGI(TAG, "Successfully erased program from storage");
    return true;
}
