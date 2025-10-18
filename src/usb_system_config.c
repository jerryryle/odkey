#include "usb_system_config.h"
#include <string.h>
#include "esp_log.h"
#include "program_storage.h"
#include "tinyusb.h"

static const char *TAG = "usb_system_config";

// Command codes for Raw HID protocol (first byte of 64-byte packet)
#define RESP_OK 0x10     // Success response
#define RESP_ERROR 0x11  // Error response

#define CMD_PROGRAM_WRITE_START 0x20   // Start write session (erase + init)
#define CMD_PROGRAM_WRITE_CHUNK 0x21   // Write 60-byte data chunk (bytes 4-63)
#define CMD_PROGRAM_WRITE_FINISH 0x22  // Finish write (commit with program size)
#define CMD_PROGRAM_READ_START 0x23    // Start read session (get program size)
#define CMD_PROGRAM_READ_CHUNK 0x24    // Read next 60-byte chunk

// Upload/Download state
typedef enum {
    TRANSFER_STATE_IDLE,
    TRANSFER_STATE_WRITING,
    TRANSFER_STATE_READING,
    TRANSFER_STATE_ERROR
} transfer_state_t;

static struct
{
    transfer_state_t state;
    size_t expected_program_size;
    size_t bytes_written;
    size_t bytes_read;
    size_t total_program_size;
    const uint8_t *program_data;
    uint8_t interface_num;
    program_upload_start_callback_t on_upload_start;
} g_transfer_state = {0};

bool usb_system_config_init(uint8_t interface_num, program_upload_start_callback_t on_upload_start) {
    // Reset transfer state
    g_transfer_state.state = TRANSFER_STATE_IDLE;
    g_transfer_state.expected_program_size = 0;
    g_transfer_state.bytes_written = 0;
    g_transfer_state.bytes_read = 0;
    g_transfer_state.total_program_size = 0;
    g_transfer_state.program_data = NULL;
    g_transfer_state.interface_num = interface_num;
    g_transfer_state.on_upload_start = on_upload_start;

    ESP_LOGI(TAG, "Program transfer module initialized on interface %d", interface_num);
    return true;
}

// Helper function to extract 32-bit value from data
static uint32_t extract_u32(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void send_response(uint8_t response_id) {
    if (!tud_hid_ready()) {
        ESP_LOGW(TAG, "HID not ready for response");
        return;
    }

    // Send response packet (64 bytes with command code in first byte)
    uint8_t response_data[64] = {0};
    response_data[0] = response_id;
    tud_hid_n_report(g_transfer_state.interface_num, 0, response_data, sizeof(response_data));

    ESP_LOGD(TAG, "Sent response: 0x%02X", response_id);
}

static void send_response_with_data(uint8_t response_id, const uint8_t *data, size_t data_len) {
    if (!tud_hid_ready()) {
        ESP_LOGW(TAG, "HID not ready for response");
        return;
    }

    // Send response packet (64 bytes with command code in first byte)
    uint8_t response_data[64] = {0};
    response_data[0] = response_id;

    // Copy data to bytes 4-63 (60 bytes max)
    size_t copy_len = (data_len > 60) ? 60 : data_len;
    if (data && copy_len > 0) {
        memcpy(&response_data[4], data, copy_len);
    }

    tud_hid_n_report(g_transfer_state.interface_num, 0, response_data, sizeof(response_data));

    ESP_LOGD(TAG, "Sent response: 0x%02X with %lu bytes data", response_id, (unsigned long)copy_len);
}

// Handle CMD_PROGRAM_WRITE_START command
static void handle_program_write_start(uint32_t program_size) {
    if ((program_size == 0) || (program_size > PROGRAM_STORAGE_MAX_SIZE)) {
        ESP_LOGE(TAG, "Invalid program size: %lu", (unsigned long)program_size);
        send_response(RESP_ERROR);
        return;
    }

    // Notify that program upload is starting
    if (g_transfer_state.on_upload_start) {
        ESP_LOGI(TAG, "Program upload starting - notifying callback");
        if (!g_transfer_state.on_upload_start()) {
            ESP_LOGW(TAG, "Program upload aborted by callback");
            send_response(RESP_ERROR);
            return;
        }
    }

    // Calculate storage size needed (round up to 60-byte chunk boundary)
    size_t expected_program_storage_size = ((program_size + 59) / 60) * 60;

    if (!program_storage_write_start(expected_program_storage_size)) {
        ESP_LOGE(TAG, "Failed to start program storage write");
        send_response(RESP_ERROR);
        return;
    }

    g_transfer_state.state = TRANSFER_STATE_WRITING;
    g_transfer_state.expected_program_size = expected_program_storage_size;  // Track storage size for chunk validation
    g_transfer_state.bytes_written = 0;

    ESP_LOGI(TAG, "Write session started, program: %lu bytes, storage: %lu bytes", (unsigned long)program_size, (unsigned long)expected_program_storage_size);
    send_response(RESP_OK);
}

// Handle CMD_PROGRAM_WRITE_CHUNK command
static void handle_program_write_chunk(const uint8_t *chunk_data, uint16_t chunk_size) {
    if (g_transfer_state.state != TRANSFER_STATE_WRITING) {
        ESP_LOGE(TAG, "PROGRAM_WRITE_CHUNK received but not in writing state");
        send_response(RESP_ERROR);
        return;
    }

    if (chunk_size != 60) {
        ESP_LOGE(TAG, "PROGRAM_WRITE_CHUNK must be exactly 60 bytes, got %d", chunk_size);
        send_response(RESP_ERROR);
        return;
    }

    // Check if writing this chunk would exceed the expected program size
    if (g_transfer_state.bytes_written + chunk_size > g_transfer_state.expected_program_size) {
        ESP_LOGE(TAG, "Chunk would exceed expected program size: %lu + %d > %lu", (unsigned long)g_transfer_state.bytes_written, chunk_size, (unsigned long)g_transfer_state.expected_program_size);
        g_transfer_state.state = TRANSFER_STATE_ERROR;
        send_response(RESP_ERROR);
        return;
    }

    // Write the 60-byte chunk to flash
    if (!program_storage_write_chunk(chunk_data, chunk_size)) {
        ESP_LOGE(TAG, "Failed to write chunk to flash");
        g_transfer_state.state = TRANSFER_STATE_ERROR;
        send_response(RESP_ERROR);
        return;
    }

    g_transfer_state.bytes_written += chunk_size;
    ESP_LOGD(TAG, "Wrote chunk: %lu/%lu bytes (storage)", (unsigned long)g_transfer_state.bytes_written, (unsigned long)g_transfer_state.expected_program_size);

    send_response(RESP_OK);
}

// Handle CMD_PROGRAM_WRITE_FINISH command
static void handle_program_write_finish(uint32_t program_size) {
    if (g_transfer_state.state != TRANSFER_STATE_WRITING) {
        ESP_LOGE(TAG, "PROGRAM_WRITE_FINISH received but not in writing state");
        send_response(RESP_ERROR);
        return;
    }
    if ((program_size == 0) || (program_size > PROGRAM_STORAGE_MAX_SIZE)) {
        ESP_LOGE(TAG, "Invalid program size: %lu", (unsigned long)program_size);
        send_response(RESP_ERROR);
        return;
    }

    if (!program_storage_write_finish(program_size)) {
        ESP_LOGE(TAG, "Failed to finish program storage write");
        send_response(RESP_ERROR);
        return;
    }

    ESP_LOGI(TAG, "Write session completed successfully: %lu bytes", (unsigned long)program_size);

    g_transfer_state.state = TRANSFER_STATE_IDLE;
    g_transfer_state.expected_program_size = 0;
    g_transfer_state.bytes_written = 0;

    send_response(RESP_OK);
}

// Handle CMD_PROGRAM_READ_START command
static void handle_program_read_start(void) {
    // Get program from storage
    size_t program_size;
    const uint8_t *program_data = program_storage_get(&program_size);

    if (program_data == NULL || program_size == 0) {
        ESP_LOGE(TAG, "No program stored in flash");
        send_response(RESP_ERROR);
        return;
    }

    // Initialize read state
    g_transfer_state.state = TRANSFER_STATE_READING;
    g_transfer_state.total_program_size = program_size;
    g_transfer_state.bytes_read = 0;
    g_transfer_state.program_data = program_data;

    ESP_LOGI(TAG, "Read session started, program: %lu bytes", (unsigned long)program_size);

    // Send response with program size in bytes 4-7
    uint8_t size_data[4];
    size_data[0] = (uint8_t)(program_size & 0xFF);
    size_data[1] = (uint8_t)((program_size >> 8) & 0xFF);
    size_data[2] = (uint8_t)((program_size >> 16) & 0xFF);
    size_data[3] = (uint8_t)((program_size >> 24) & 0xFF);

    send_response_with_data(RESP_OK, size_data, 4);
}

// Handle CMD_PROGRAM_READ_CHUNK command
static void handle_program_read_chunk(void) {
    if (g_transfer_state.state != TRANSFER_STATE_READING) {
        ESP_LOGE(TAG, "PROGRAM_READ_CHUNK received but not in reading state");
        send_response(RESP_ERROR);
        return;
    }

    if (g_transfer_state.bytes_read >= g_transfer_state.total_program_size) {
        ESP_LOGE(TAG, "All program data already read");
        send_response(RESP_ERROR);
        return;
    }

    // Calculate how many bytes to send (max 60)
    size_t bytes_remaining = g_transfer_state.total_program_size - g_transfer_state.bytes_read;
    size_t chunk_size = (bytes_remaining > 60) ? 60 : bytes_remaining;

    // Get chunk data
    const uint8_t *chunk_data = g_transfer_state.program_data + g_transfer_state.bytes_read;

    // Pad chunk to 60 bytes if needed
    uint8_t padded_chunk[60] = {0};
    memcpy(padded_chunk, chunk_data, chunk_size);

    // Update read position
    g_transfer_state.bytes_read += chunk_size;

    ESP_LOGD(TAG, "Read chunk: %lu/%lu bytes", (unsigned long)g_transfer_state.bytes_read, (unsigned long)g_transfer_state.total_program_size);

    // Send chunk data
    send_response_with_data(RESP_OK, padded_chunk, 60);

    // If we've read all data, reset state
    if (g_transfer_state.bytes_read >= g_transfer_state.total_program_size) {
        ESP_LOGI(TAG, "Read session completed successfully: %lu bytes", (unsigned long)g_transfer_state.total_program_size);
        g_transfer_state.state = TRANSFER_STATE_IDLE;
        g_transfer_state.total_program_size = 0;
        g_transfer_state.bytes_read = 0;
        g_transfer_state.program_data = NULL;
    }
}

void usb_system_config_process_command(const uint8_t *data, uint16_t len) {
    // Every command must have at least the command code (4 bytes)
    if (data == NULL || len < 4) {
        ESP_LOGE(TAG, "Invalid command data");
        send_response(RESP_ERROR);
        return;
    }

    // Debug: Print what we received
    ESP_LOGD(TAG, "Received command: len=%d, data[0]=0x%02X, first 8 bytes: %02X %02X %02X %02X %02X %02X %02X %02X", len, data[0], data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);

    uint8_t command = data[0];  // Command code is currently only on the first byte

    switch (command) {
    case CMD_PROGRAM_WRITE_START:
        if (len < 8)  // Need command code + 4 bytes for program size
        {
            ESP_LOGE(TAG, "PROGRAM_WRITE_START command too short");
            send_response(RESP_ERROR);
            return;
        } else {
            uint32_t program_size = extract_u32(&data[4]);  // Program size is in bytes 4-7
            handle_program_write_start(program_size);
        }
        break;

    case CMD_PROGRAM_WRITE_CHUNK:
        if (len != 64)  // Must be exactly 64 bytes
        {
            ESP_LOGE(TAG, "PROGRAM_WRITE_CHUNK must be exactly 64 bytes, got %d", len);
            send_response(RESP_ERROR);
            return;
        } else {
            const uint8_t *chunk_data = &data[4];  // Data payload is in bytes 4-63
            handle_program_write_chunk(chunk_data, 60);
        }
        break;

    case CMD_PROGRAM_WRITE_FINISH:
        if (len < 8)  // Need command code + 4 bytes for program size
        {
            ESP_LOGE(TAG, "PROGRAM_WRITE_FINISH command too short");
            send_response(RESP_ERROR);
            return;
        } else {
            uint32_t program_size = extract_u32(&data[4]);  // Program size is in bytes 4-7
            handle_program_write_finish(program_size);
        }
        break;

    case CMD_PROGRAM_READ_START:
        handle_program_read_start();
        break;

    case CMD_PROGRAM_READ_CHUNK:
        handle_program_read_chunk();
        break;

    default:
        ESP_LOGW(TAG, "Unknown command: 0x%02X", command);
        send_response(RESP_ERROR);
        break;
    }
}
