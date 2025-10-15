#include "program_upload.h"
#include "program_storage.h"
#include "tinyusb.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "program_upload";

// Command codes for Raw HID protocol (first byte of 64-byte packet)
#define CMD_WRITE_START 0x10  // Start write session (erase + init)
#define CMD_WRITE_CHUNK 0x11  // Write 60-byte data chunk (bytes 4-63)
#define CMD_WRITE_FINISH 0x12 // Finish write (commit with program size)
#define RESP_OK 0x20          // Success response
#define RESP_ERROR 0x21       // Error response

// Upload state
typedef enum
{
    UPLOAD_STATE_IDLE,
    UPLOAD_STATE_WRITING,
    UPLOAD_STATE_ERROR
} upload_state_t;

static struct
{
    upload_state_t state;
    size_t expected_program_size;
    size_t bytes_written;
    uint8_t interface_num;
} g_upload_state = {0};

bool program_upload_init(uint8_t interface_num)
{
    // Reset upload state
    g_upload_state.state = UPLOAD_STATE_IDLE;
    g_upload_state.expected_program_size = 0;
    g_upload_state.bytes_written = 0;
    g_upload_state.interface_num = interface_num;

    ESP_LOGI(TAG, "Program upload module initialized on interface %d", interface_num);
    return true;
}

// Helper function to extract 32-bit value from data
static uint32_t extract_u32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void send_response(uint8_t response_id)
{
    if (!tud_hid_ready())
    {
        ESP_LOGW(TAG, "HID not ready for response");
        return;
    }

    // Send response packet (64 bytes with command code in first byte)
    uint8_t response_data[64] = {0};
    response_data[0] = response_id;
    tud_hid_n_report(g_upload_state.interface_num, 0, response_data, sizeof(response_data));

    ESP_LOGD(TAG, "Sent response: 0x%02X", response_id);
}

// Handle CMD_WRITE_START command
static void handle_write_start(uint32_t program_size)
{
    if ((program_size == 0) || (program_size > PROGRAM_STORAGE_MAX_SIZE))
    {
        ESP_LOGE(TAG, "Invalid program size: %lu", (unsigned long)program_size);
        send_response(RESP_ERROR);
        return;
    }

    if (!program_storage_write_start())
    {
        ESP_LOGE(TAG, "Failed to start program storage write");
        send_response(RESP_ERROR);
        return;
    }

    g_upload_state.state = UPLOAD_STATE_WRITING;
    g_upload_state.expected_program_size = program_size;
    g_upload_state.bytes_written = 0;

    ESP_LOGI(TAG, "Write session started, expected size: %lu bytes", (unsigned long)program_size);
    send_response(RESP_OK);
}

// Handle CMD_WRITE_CHUNK command
static void handle_write_chunk(const uint8_t *chunk_data, uint16_t chunk_size)
{
    if (g_upload_state.state != UPLOAD_STATE_WRITING)
    {
        ESP_LOGE(TAG, "WRITE_CHUNK received but not in writing state");
        send_response(RESP_ERROR);
        return;
    }

    if (chunk_size != 60)
    {
        ESP_LOGE(TAG, "WRITE_CHUNK must be exactly 60 bytes, got %d", chunk_size);
        send_response(RESP_ERROR);
        return;
    }

    // Write the 60-byte chunk to flash
    if (!program_storage_write_chunk(chunk_data, chunk_size))
    {
        ESP_LOGE(TAG, "Failed to write chunk to flash");
        g_upload_state.state = UPLOAD_STATE_ERROR;
        send_response(RESP_ERROR);
        return;
    }

    g_upload_state.bytes_written += chunk_size;
    ESP_LOGD(TAG, "Wrote chunk: %lu/%lu bytes",
             (unsigned long)g_upload_state.bytes_written,
             (unsigned long)g_upload_state.expected_program_size);

    send_response(RESP_OK);
}

// Handle CMD_WRITE_FINISH command
static void handle_write_finish(uint32_t program_size)
{
    if (g_upload_state.state != UPLOAD_STATE_WRITING)
    {
        ESP_LOGE(TAG, "WRITE_FINISH received but not in writing state");
        send_response(RESP_ERROR);
        return;
    }
    if ((program_size == 0) || (program_size > PROGRAM_STORAGE_MAX_SIZE))
    {
        ESP_LOGE(TAG, "Invalid program size: %lu", (unsigned long)program_size);
        send_response(RESP_ERROR);
        return;
    }

    if (!program_storage_write_finish(program_size))
    {
        ESP_LOGE(TAG, "Failed to finish program storage write");
        send_response(RESP_ERROR);
        return;
    }

    ESP_LOGI(TAG, "Write session completed successfully: %lu bytes", (unsigned long)program_size);

    g_upload_state.state = UPLOAD_STATE_IDLE;
    g_upload_state.expected_program_size = 0;
    g_upload_state.bytes_written = 0;

    send_response(RESP_OK);
}

void program_upload_process_command(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0)
    {
        ESP_LOGE(TAG, "Invalid command data");
        send_response(RESP_ERROR);
        return;
    }

    uint8_t command = data[0]; // Command code is in first byte

    switch (command)
    {
    case CMD_WRITE_START:
        if (len < 8) // Need command code + 4 bytes for program size
        {
            ESP_LOGE(TAG, "WRITE_START command too short");
            send_response(RESP_ERROR);
            return;
        }
        else
        {
            uint32_t program_size = extract_u32(&data[4]); // Program size is in bytes 4-7
            handle_write_start(program_size);
        }
        break;

    case CMD_WRITE_CHUNK:
        if (len != 64) // Must be exactly 64 bytes
        {
            ESP_LOGE(TAG, "WRITE_CHUNK must be exactly 64 bytes, got %d", len);
            send_response(RESP_ERROR);
            return;
        }
        else
        {
            const uint8_t *chunk_data = &data[4]; // Data payload is in bytes 4-63
            handle_write_chunk(chunk_data, 60);
        }
        break;

    case CMD_WRITE_FINISH:
        if (len < 8) // Need command code + 4 bytes for program size
        {
            ESP_LOGE(TAG, "WRITE_FINISH command too short");
            send_response(RESP_ERROR);
            return;
        }
        else
        {
            uint32_t program_size = extract_u32(&data[4]); // Program size is in bytes 4-7
            handle_write_finish(program_size);
        }
        break;

    default:
        ESP_LOGW(TAG, "Unknown command: 0x%02X", command);
        send_response(RESP_ERROR);
        break;
    }
}
