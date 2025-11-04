#include "usb_system_config.h"
#include <string.h>
#include "buffer_utils.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "log_buffer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "program.h"
#include "tinyusb.h"

static const char *TAG = "usb_system_config";

// NVS transfer buffer size
#define NVS_TRANSFER_BUFFER_SIZE 1024

// Command processing task configuration
#define COMMAND_QUEUE_DEPTH 5
#define COMMAND_TASK_STACK_SIZE 4096
#define COMMAND_TASK_PRIORITY 4

// Command queue item structure
typedef struct {
    uint8_t data[64];  // Full 64-byte packet
    uint16_t len;      // Actual length received
} command_queue_item_t;

// Command codes for Raw HID protocol (first byte of 64-byte packet)
#define RESP_OK 0x10     // Success response
#define RESP_ERROR 0x11  // Error response

#define CMD_FLASH_PROGRAM_WRITE_START 0x20   // Start FLASH program write session
#define CMD_FLASH_PROGRAM_WRITE_CHUNK 0x21   // Write FLASH program data chunk
#define CMD_FLASH_PROGRAM_WRITE_FINISH 0x22  // Finish FLASH program write session
#define CMD_FLASH_PROGRAM_READ_START 0x23    // Start FLASH program read session
#define CMD_FLASH_PROGRAM_READ_CHUNK 0x24    // Read next FLASH program data chunk
#define CMD_FLASH_PROGRAM_EXECUTE 0x25       // Execute FLASH program
#define CMD_RAM_PROGRAM_WRITE_START 0x26     // Start RAM program write session
#define CMD_RAM_PROGRAM_WRITE_CHUNK 0x27     // Write RAM program data chunk
#define CMD_RAM_PROGRAM_WRITE_FINISH 0x28    // Finish RAM program write session
#define CMD_RAM_PROGRAM_READ_START 0x29      // Start RAM program read session
#define CMD_RAM_PROGRAM_READ_CHUNK 0x2A      // Read next RAM program data chunk
#define CMD_RAM_PROGRAM_EXECUTE 0x2B         // Execute RAM program
#define CMD_NVS_SET_START 0x30               // Start NVS set operation
#define CMD_NVS_SET_DATA 0x31                // Send NVS value data chunk
#define CMD_NVS_SET_FINISH 0x32              // Finish NVS set operation
#define CMD_NVS_GET_START 0x33               // Start NVS get operation
#define CMD_NVS_GET_DATA 0x34                // Read NVS value data chunk
#define CMD_NVS_DELETE 0x35                  // Delete NVS key
#define CMD_LOG_READ_START 0x40              // Start streaming logs
#define CMD_LOG_READ_CHUNK 0x41              // Device: Log data chunk (sent by device)
#define CMD_LOG_READ_END 0x42                // Device: No more log data available
#define CMD_LOG_READ_STOP 0x43               // Stop streaming logs
#define CMD_LOG_CLEAR 0x44                   // Clear the log buffer

// Upload/Download/NVS state
typedef enum {
    TRANSFER_STATE_IDLE,
    TRANSFER_STATE_WRITING,
    TRANSFER_STATE_READING,
    TRANSFER_STATE_NVS_SETTING,
    TRANSFER_STATE_NVS_GETTING,
    TRANSFER_STATE_RAM_WRITING,
    TRANSFER_STATE_LOG_STREAMING,
    TRANSFER_STATE_ERROR
} transfer_state_t;

static struct {
    transfer_state_t state;
    size_t total_program_size;
    size_t program_bytes_read;
    const uint8_t *program_data;
    uint8_t interface_num;

    // NVS transfer state
    uint8_t nvs_value_type;
    char nvs_key[NVS_KEY_NAME_MAX_SIZE];
    size_t nvs_value_length;

    // NVS transfer buffer
    uint8_t nvs_transfer_buffer[NVS_TRANSFER_BUFFER_SIZE];
    size_t nvs_transfer_buffer_transferred;
} g_transfer_state = {0};

// Command processing queue and task
static QueueHandle_t g_command_queue = NULL;
static TaskHandle_t g_command_task_handle = NULL;

// Forward declarations
static void process_command_internal(const uint8_t *data, uint16_t len);

// Helper function to reset NVS transfer buffer
static void reset_nvs_transfer_buffer(void) {
    memset(g_transfer_state.nvs_transfer_buffer,
           0,
           sizeof(g_transfer_state.nvs_transfer_buffer));
    g_transfer_state.nvs_transfer_buffer_transferred = 0;
}

// Command processing task function
static void command_processing_task(void *pvParameters) {
    ESP_LOGI(TAG, "Command processing task started");

    command_queue_item_t cmd_item;
    for (;;) {
        // Wait for a command from the queue
        if (xQueueReceive(g_command_queue, &cmd_item, portMAX_DELAY) == pdTRUE) {
            // Process the command (this can now block safely)
            process_command_internal(cmd_item.data, cmd_item.len);
        }
    }
}

bool usb_system_config_init(uint8_t interface_num) {
    // Reset transfer state
    g_transfer_state.state = TRANSFER_STATE_IDLE;
    g_transfer_state.total_program_size = 0;
    g_transfer_state.program_bytes_read = 0;
    g_transfer_state.program_data = NULL;
    g_transfer_state.interface_num = interface_num;
    // Reset NVS state
    g_transfer_state.nvs_value_type = 0;
    memset(g_transfer_state.nvs_key, 0, sizeof(g_transfer_state.nvs_key));
    g_transfer_state.nvs_value_length = 0;

    // Reset NVS transfer buffer
    reset_nvs_transfer_buffer();

    // Create command queue
    g_command_queue = xQueueCreate(COMMAND_QUEUE_DEPTH, sizeof(command_queue_item_t));
    if (g_command_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create command queue");
        return false;
    }

    // Create command processing task
    BaseType_t ret = xTaskCreate(command_processing_task,
                                 "usb_cmd",
                                 COMMAND_TASK_STACK_SIZE,
                                 NULL,
                                 COMMAND_TASK_PRIORITY,
                                 &g_command_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create command processing task");
        vQueueDelete(g_command_queue);
        g_command_queue = NULL;
        return false;
    }

    ESP_LOGI(
        TAG, "System configuration module initialized on interface %d", interface_num);
    return true;
}

static void send_response(uint8_t response_id) {
    // Wait for USB HID to be ready before sending
    while (!tud_hid_ready()) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // Send response packet (64 bytes with command code in first byte)
    uint8_t response_data[64] = {0};
    response_data[0] = response_id;
    bool success = tud_hid_n_report(
        g_transfer_state.interface_num, 0, response_data, sizeof(response_data));

    if (!success) {
        ESP_LOGE(TAG, "tud_hid_n_report FAILED for response: 0x%02X", response_id);
    } else {
        ESP_LOGD(TAG, "Sent response: 0x%02X", response_id);
    }
}

static void send_response_with_data(uint8_t response_id,
                                    const uint8_t *data,
                                    size_t data_len) {
    // Wait for USB HID to be ready before sending
    while (!tud_hid_ready()) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // Send response packet (64 bytes with command code in first byte)
    uint8_t response_data[64] = {0};
    response_data[0] = response_id;

    // Copy data to bytes 4-63 (60 bytes max)
    size_t copy_len = (data_len > 60) ? 60 : data_len;
    if (data && copy_len > 0) {
        memcpy(&response_data[4], data, copy_len);
    }

    bool success = tud_hid_n_report(
        g_transfer_state.interface_num, 0, response_data, sizeof(response_data));

    if (!success) {
        ESP_LOGE(TAG,
                 "tud_hid_n_report FAILED for response: 0x%02X with %lu bytes data",
                 response_id,
                 (unsigned long)copy_len);
    } else {
        ESP_LOGD(TAG,
                 "Sent response: 0x%02X with %lu bytes data",
                 response_id,
                 (unsigned long)copy_len);
    }
}

// Handle CMD_FLASH_PROGRAM_WRITE_START command
static void handle_flash_program_write_start(uint32_t program_size) {
    if ((program_size == 0) || (program_size > PROGRAM_FLASH_MAX_SIZE)) {
        ESP_LOGE(TAG, "Invalid program size: %lu", (unsigned long)program_size);
        send_response(RESP_ERROR);
        return;
    }

    // Notify that program upload is starting
    ESP_LOGI(TAG, "FLASH program upload starting");

    // Start program storage write session
    if (!program_write_start(
            PROGRAM_TYPE_FLASH, program_size, PROGRAM_WRITE_SOURCE_USB)) {
        ESP_LOGE(TAG, "Failed to start program storage write session");
        send_response(RESP_ERROR);
        return;
    }

    g_transfer_state.state = TRANSFER_STATE_WRITING;

    ESP_LOGI(
        TAG, "Write session started, program: %lu bytes", (unsigned long)program_size);
    send_response(RESP_OK);
}

// Handle CMD_FLASH_PROGRAM_WRITE_CHUNK command
static void handle_flash_program_write_chunk(const uint8_t *chunk_data,
                                             uint16_t chunk_size) {
    if (g_transfer_state.state != TRANSFER_STATE_WRITING) {
        ESP_LOGE(TAG, "PROGRAM_WRITE_CHUNK received but not in writing state");
        send_response(RESP_ERROR);
        return;
    }

    if (chunk_size != 60) {
        ESP_LOGE(
            TAG, "PROGRAM_WRITE_CHUNK must be exactly 60 bytes, got %d", chunk_size);
        g_transfer_state.state = TRANSFER_STATE_ERROR;
        send_response(RESP_ERROR);
        return;
    }

    // Calculate how many bytes of actual program data are in this chunk
    size_t expected_size = program_get_expected_size(PROGRAM_TYPE_FLASH);
    size_t bytes_written = program_get_bytes_written(PROGRAM_TYPE_FLASH);
    size_t program_bytes_remaining = expected_size - bytes_written;
    size_t actual_chunk_size =
        (program_bytes_remaining < chunk_size) ? program_bytes_remaining : chunk_size;

    // Write chunk to program storage
    if (!program_write_chunk(PROGRAM_TYPE_FLASH,
                             chunk_data,
                             actual_chunk_size,
                             PROGRAM_WRITE_SOURCE_USB)) {
        ESP_LOGE(TAG, "Failed to write chunk to program storage");
        g_transfer_state.state = TRANSFER_STATE_ERROR;
        send_response(RESP_ERROR);
        return;
    }

    ESP_LOGD(TAG,
             "Buffered chunk: %lu/%lu bytes (program)",
             (unsigned long)program_get_bytes_written(PROGRAM_TYPE_FLASH),
             (unsigned long)expected_size);

    send_response(RESP_OK);
}

// Handle CMD_FLASH_PROGRAM_WRITE_FINISH command
static void handle_flash_program_write_finish(uint32_t program_size) {
    if (g_transfer_state.state != TRANSFER_STATE_WRITING) {
        ESP_LOGE(TAG, "PROGRAM_WRITE_FINISH received but not in writing state");
        send_response(RESP_ERROR);
        return;
    }
    if ((program_size == 0) || (program_size > PROGRAM_FLASH_MAX_SIZE)) {
        ESP_LOGE(TAG, "Invalid program size: %lu", (unsigned long)program_size);
        send_response(RESP_ERROR);
        return;
    }

    // Finish program storage write session
    if (!program_write_finish(
            PROGRAM_TYPE_FLASH, program_size, PROGRAM_WRITE_SOURCE_USB)) {
        ESP_LOGE(TAG, "Failed to finish program storage write session");
        g_transfer_state.state = TRANSFER_STATE_ERROR;
        send_response(RESP_ERROR);
        return;
    }

    ESP_LOGI(TAG,
             "Write session completed successfully: %lu bytes",
             (unsigned long)program_size);

    g_transfer_state.state = TRANSFER_STATE_IDLE;

    send_response(RESP_OK);
}

// Handle CMD_FLASH_PROGRAM_READ_START command
static void handle_flash_program_read_start(void) {
    // Get program from storage
    uint32_t program_size;
    const uint8_t *program_data = program_get(PROGRAM_TYPE_FLASH, &program_size);

    if (program_data == NULL || program_size == 0) {
        ESP_LOGE(TAG, "No program stored in flash");
        send_response(RESP_ERROR);
        return;
    }

    // Initialize read state
    g_transfer_state.state = TRANSFER_STATE_READING;
    g_transfer_state.total_program_size = program_size;
    g_transfer_state.program_bytes_read = 0;
    g_transfer_state.program_data = program_data;

    ESP_LOGI(
        TAG, "Read session started, program: %lu bytes", (unsigned long)program_size);

    // Send response with program size in bytes 4-7
    uint8_t size_data[4];
    size_data[0] = (uint8_t)(program_size & 0xFF);
    size_data[1] = (uint8_t)((program_size >> 8) & 0xFF);
    size_data[2] = (uint8_t)((program_size >> 16) & 0xFF);
    size_data[3] = (uint8_t)((program_size >> 24) & 0xFF);

    send_response_with_data(RESP_OK, size_data, 4);
}

// Handle CMD_FLASH_PROGRAM_READ_CHUNK and CMD_RAM_PROGRAM_READ_CHUNK command
static void handle_program_read_chunk(void) {
    if (g_transfer_state.state != TRANSFER_STATE_READING) {
        ESP_LOGE(TAG, "PROGRAM_READ_CHUNK received but not in reading state");
        send_response(RESP_ERROR);
        return;
    }

    if (g_transfer_state.program_bytes_read >= g_transfer_state.total_program_size) {
        ESP_LOGE(TAG, "All program data already read");
        g_transfer_state.state = TRANSFER_STATE_ERROR;
        send_response(RESP_ERROR);
        return;
    }

    // Calculate how many bytes to send (max 60)
    size_t bytes_remaining =
        g_transfer_state.total_program_size - g_transfer_state.program_bytes_read;
    size_t chunk_size = (bytes_remaining > 60) ? 60 : bytes_remaining;

    // Get chunk data
    const uint8_t *chunk_data =
        g_transfer_state.program_data + g_transfer_state.program_bytes_read;

    // Pad chunk to 60 bytes if needed
    uint8_t padded_chunk[60] = {0};
    memcpy(padded_chunk, chunk_data, chunk_size);

    // Update read position
    g_transfer_state.program_bytes_read += chunk_size;

    ESP_LOGD(TAG,
             "Read chunk: %lu/%lu bytes",
             (unsigned long)g_transfer_state.program_bytes_read,
             (unsigned long)g_transfer_state.total_program_size);

    // Send chunk data
    send_response_with_data(RESP_OK, padded_chunk, 60);

    // If we've read all data, reset state
    if (g_transfer_state.program_bytes_read >= g_transfer_state.total_program_size) {
        ESP_LOGI(TAG,
                 "Read session completed successfully: %lu bytes",
                 (unsigned long)g_transfer_state.total_program_size);
        g_transfer_state.state = TRANSFER_STATE_IDLE;
        g_transfer_state.total_program_size = 0;
        g_transfer_state.program_bytes_read = 0;
        g_transfer_state.program_data = NULL;
    }
}

// Handle CMD_FLASH_PROGRAM_EXECUTE command
static void handle_flash_program_execute(void) {
    if (!program_execute(PROGRAM_TYPE_FLASH, NULL, NULL)) {
        ESP_LOGW(TAG, "FLASH program execution failed");
        send_response(RESP_ERROR);
        return;
    }
    ESP_LOGI(TAG, "FLASH program execution started");
    send_response(RESP_OK);
}

// Handle CMD_RAM_PROGRAM_WRITE_START command
static void handle_ram_program_write_start(uint32_t program_size) {
    if ((program_size == 0) || (program_size > PROGRAM_RAM_MAX_SIZE)) {
        ESP_LOGE(TAG, "Invalid RAM program size: %lu", (unsigned long)program_size);
        send_response(RESP_ERROR);
        return;
    }

    // Notify that program upload is starting
    ESP_LOGI(TAG, "RAM program upload starting");

    // Start RAM program storage write session
    if (!program_write_start(
            PROGRAM_TYPE_RAM, program_size, PROGRAM_WRITE_SOURCE_USB)) {
        ESP_LOGE(TAG, "Failed to start RAM program storage write session");
        send_response(RESP_ERROR);
        return;
    }

    g_transfer_state.state = TRANSFER_STATE_RAM_WRITING;

    ESP_LOGI(TAG,
             "RAM write session started, program: %lu bytes",
             (unsigned long)program_size);
    send_response(RESP_OK);
}

// Handle CMD_RAM_PROGRAM_WRITE_CHUNK command
static void handle_ram_program_write_chunk(const uint8_t *chunk_data,
                                           uint16_t chunk_size) {
    if (g_transfer_state.state != TRANSFER_STATE_RAM_WRITING) {
        ESP_LOGE(TAG, "RAM_PROGRAM_WRITE_CHUNK received but not in RAM writing state");
        send_response(RESP_ERROR);
        return;
    }

    if (chunk_size != 60) {
        ESP_LOGE(TAG,
                 "RAM_PROGRAM_WRITE_CHUNK must be exactly 60 bytes, got %d",
                 chunk_size);
        g_transfer_state.state = TRANSFER_STATE_ERROR;
        send_response(RESP_ERROR);
        return;
    }

    // Calculate how many bytes of actual program data are in this chunk
    size_t expected_size = program_get_expected_size(PROGRAM_TYPE_RAM);
    size_t bytes_written = program_get_bytes_written(PROGRAM_TYPE_RAM);
    size_t program_bytes_remaining = expected_size - bytes_written;
    size_t actual_chunk_size =
        (program_bytes_remaining < chunk_size) ? program_bytes_remaining : chunk_size;

    // Write chunk to RAM program storage
    if (!program_write_chunk(PROGRAM_TYPE_RAM,
                             chunk_data,
                             actual_chunk_size,
                             PROGRAM_WRITE_SOURCE_USB)) {
        ESP_LOGE(TAG, "Failed to write chunk to RAM program storage");
        g_transfer_state.state = TRANSFER_STATE_ERROR;
        send_response(RESP_ERROR);
        return;
    }

    ESP_LOGD(TAG,
             "Buffered RAM chunk: %lu/%lu bytes (program)",
             (unsigned long)program_get_bytes_written(PROGRAM_TYPE_RAM),
             (unsigned long)expected_size);

    send_response(RESP_OK);
}

// Handle CMD_RAM_PROGRAM_WRITE_FINISH command
static void handle_ram_program_write_finish(uint32_t program_size) {
    if (g_transfer_state.state != TRANSFER_STATE_RAM_WRITING) {
        ESP_LOGE(TAG, "RAM_PROGRAM_WRITE_FINISH received but not in RAM writing state");
        send_response(RESP_ERROR);
        return;
    }
    if ((program_size == 0) || (program_size > PROGRAM_RAM_MAX_SIZE)) {
        ESP_LOGE(TAG, "Invalid RAM program size: %lu", (unsigned long)program_size);
        send_response(RESP_ERROR);
        return;
    }

    // Finish RAM program storage write session
    if (!program_write_finish(
            PROGRAM_TYPE_RAM, program_size, PROGRAM_WRITE_SOURCE_USB)) {
        ESP_LOGE(TAG, "Failed to finish RAM program storage write session");
        g_transfer_state.state = TRANSFER_STATE_ERROR;
        send_response(RESP_ERROR);
        return;
    }

    ESP_LOGI(TAG,
             "RAM write session completed successfully: %lu bytes",
             (unsigned long)program_size);

    g_transfer_state.state = TRANSFER_STATE_IDLE;

    send_response(RESP_OK);
}

// Handle CMD_RAM_PROGRAM_EXECUTE command
static void handle_ram_program_execute(void) {
    if (!program_execute(PROGRAM_TYPE_RAM, NULL, NULL)) {
        ESP_LOGW(TAG, "RAM program execution failed");
        send_response(RESP_ERROR);
        return;
    }
    ESP_LOGI(TAG, "RAM program execution started");
    send_response(RESP_OK);
}

// Handle CMD_RAM_PROGRAM_READ_START command
static void handle_ram_program_read_start(void) {
    // Get RAM program from storage
    uint32_t program_size;
    const uint8_t *program_data = program_get(PROGRAM_TYPE_RAM, &program_size);

    if (program_data == NULL || program_size == 0) {
        ESP_LOGE(TAG, "No RAM program stored");
        send_response(RESP_ERROR);
        return;
    }

    // Initialize read state
    g_transfer_state.state = TRANSFER_STATE_READING;
    g_transfer_state.total_program_size = program_size;
    g_transfer_state.program_bytes_read = 0;
    g_transfer_state.program_data = program_data;

    ESP_LOGI(TAG,
             "RAM read session started, program: %lu bytes",
             (unsigned long)program_size);

    // Send response with program size in bytes 4-7
    uint8_t size_data[4];
    size_data[0] = (uint8_t)(program_size & 0xFF);
    size_data[1] = (uint8_t)((program_size >> 8) & 0xFF);
    size_data[2] = (uint8_t)((program_size >> 16) & 0xFF);
    size_data[3] = (uint8_t)((program_size >> 24) & 0xFF);

    send_response_with_data(RESP_OK, size_data, 4);
}

// Handle CMD_NVS_SET_START command
static void handle_nvs_set_start(const uint8_t *data) {
    // Parse command data: type(1) + length(4) + key(16) = 21 bytes
    uint8_t value_type = data[4];
    uint32_t value_length;
    if (!bu_read_u32_le(&data[5], 4, &value_length)) {
        ESP_LOGE(TAG, "Failed to read value length");
        send_response(RESP_ERROR);
        return;
    }

    // Copy key (null-terminated, max 15 chars)
    strncpy(g_transfer_state.nvs_key, (const char *)&data[9], 15);
    g_transfer_state.nvs_key[15] = '\0';

    // Validate key length
    size_t key_len = strlen(g_transfer_state.nvs_key);
    if (key_len == 0 || key_len > 15) {
        ESP_LOGE(TAG, "Invalid key length: %lu", (unsigned long)key_len);
        send_response(RESP_ERROR);
        return;
    }

    // Validate value length based on type
    switch (value_type) {
    case NVS_TYPE_U8:
    case NVS_TYPE_I8:
        if (value_length != 1) {
            ESP_LOGE(TAG, "Invalid length for u8/i8: %lu", (unsigned long)value_length);
            send_response(RESP_ERROR);
            return;
        }
        break;
    case NVS_TYPE_U16:
    case NVS_TYPE_I16:
        if (value_length != 2) {
            ESP_LOGE(
                TAG, "Invalid length for u16/i16: %lu", (unsigned long)value_length);
            send_response(RESP_ERROR);
            return;
        }
        break;
    case NVS_TYPE_U32:
    case NVS_TYPE_I32:
        if (value_length != 4) {
            ESP_LOGE(
                TAG, "Invalid length for u32/i32: %lu", (unsigned long)value_length);
            send_response(RESP_ERROR);
            return;
        }
        break;
    case NVS_TYPE_U64:
    case NVS_TYPE_I64:
        if (value_length != 8) {
            ESP_LOGE(
                TAG, "Invalid length for u64/i64: %lu", (unsigned long)value_length);
            send_response(RESP_ERROR);
            return;
        }
        break;
    case NVS_TYPE_STR:
    case NVS_TYPE_BLOB:
        if (value_length > 1024) {
            ESP_LOGE(TAG, "Value too large: %lu bytes", (unsigned long)value_length);
            send_response(RESP_ERROR);
            return;
        }
        break;
    default:
        ESP_LOGE(TAG, "Invalid value type: 0x%02X", value_type);
        send_response(RESP_ERROR);
        return;
    }

    // Initialize NVS set state
    g_transfer_state.nvs_value_type = value_type;
    g_transfer_state.nvs_value_length = value_length;
    g_transfer_state.state = TRANSFER_STATE_NVS_SETTING;

    // Reset NVS transfer buffer for new operation
    reset_nvs_transfer_buffer();

    ESP_LOGI(TAG,
             "NVS set started: key='%s', type=0x%02X, length=%lu",
             g_transfer_state.nvs_key,
             value_type,
             (unsigned long)value_length);

    send_response(RESP_OK);
}

// Handle CMD_NVS_SET_DATA command
static void handle_nvs_set_data(const uint8_t *data) {
    if (g_transfer_state.state != TRANSFER_STATE_NVS_SETTING) {
        ESP_LOGE(TAG, "NVS_SET_DATA received but not in setting state");
        send_response(RESP_ERROR);
        return;
    }

    // Calculate how many bytes to copy (60 bytes max)
    size_t bytes_remaining = g_transfer_state.nvs_value_length -
                             g_transfer_state.nvs_transfer_buffer_transferred;
    size_t bytes_to_copy = (bytes_remaining > 60) ? 60 : bytes_remaining;

    if (bytes_to_copy > 0) {
        // Check if buffer would overflow
        if (g_transfer_state.nvs_transfer_buffer_transferred + bytes_to_copy >
            NVS_TRANSFER_BUFFER_SIZE) {
            ESP_LOGE(TAG, "NVS transfer buffer overflow");
            g_transfer_state.state = TRANSFER_STATE_ERROR;
            send_response(RESP_ERROR);
            return;
        }

        memcpy(
            &g_transfer_state
                 .nvs_transfer_buffer[g_transfer_state.nvs_transfer_buffer_transferred],
            &data[4],
            bytes_to_copy);
        g_transfer_state.nvs_transfer_buffer_transferred += bytes_to_copy;
    }

    ESP_LOGD(TAG,
             "NVS set data: %lu/%lu bytes",
             (unsigned long)g_transfer_state.nvs_transfer_buffer_transferred,
             (unsigned long)g_transfer_state.nvs_value_length);

    send_response(RESP_OK);
}

// Handle CMD_NVS_SET_FINISH command
static void handle_nvs_set_finish(void) {
    if (g_transfer_state.state != TRANSFER_STATE_NVS_SETTING) {
        ESP_LOGE(TAG, "NVS_SET_FINISH received but not in setting state");
        send_response(RESP_ERROR);
        return;
    }

    if (g_transfer_state.nvs_transfer_buffer_transferred !=
        g_transfer_state.nvs_value_length) {
        ESP_LOGE(TAG,
                 "NVS set incomplete: received %lu, expected %lu",
                 (unsigned long)g_transfer_state.nvs_transfer_buffer_transferred,
                 (unsigned long)g_transfer_state.nvs_value_length);
        g_transfer_state.state = TRANSFER_STATE_ERROR;
        send_response(RESP_ERROR);
        return;
    }

    // Open NVS namespace
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("odkey", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        send_response(RESP_ERROR);
        return;
    }

    // Write value based on type
    err = ESP_FAIL;
    switch (g_transfer_state.nvs_value_type) {
    case NVS_TYPE_U8:
        err = nvs_set_u8(nvs_handle,
                         g_transfer_state.nvs_key,
                         g_transfer_state.nvs_transfer_buffer[0]);
        break;
    case NVS_TYPE_I8:
        err = nvs_set_i8(nvs_handle,
                         g_transfer_state.nvs_key,
                         (int8_t)g_transfer_state.nvs_transfer_buffer[0]);
        break;
    case NVS_TYPE_U16: {
        uint16_t value = g_transfer_state.nvs_transfer_buffer[0] |
                         (g_transfer_state.nvs_transfer_buffer[1] << 8);
        err = nvs_set_u16(nvs_handle, g_transfer_state.nvs_key, value);
    } break;
    case NVS_TYPE_I16: {
        int16_t value = g_transfer_state.nvs_transfer_buffer[0] |
                        (g_transfer_state.nvs_transfer_buffer[1] << 8);
        err = nvs_set_i16(nvs_handle, g_transfer_state.nvs_key, value);
    } break;
    case NVS_TYPE_U32: {
        uint32_t value;
        if (!bu_read_u32_le(g_transfer_state.nvs_transfer_buffer,
                            NVS_TRANSFER_BUFFER_SIZE,
                            &value)) {
            ESP_LOGE(TAG, "Failed to read u32 value");
            err = ESP_FAIL;
        } else {
            err = nvs_set_u32(nvs_handle, g_transfer_state.nvs_key, value);
        }
    } break;
    case NVS_TYPE_I32: {
        uint32_t value;
        if (!bu_read_u32_le(g_transfer_state.nvs_transfer_buffer,
                            NVS_TRANSFER_BUFFER_SIZE,
                            &value)) {
            ESP_LOGE(TAG, "Failed to read i32 value");
            err = ESP_FAIL;
        } else {
            err = nvs_set_i32(nvs_handle, g_transfer_state.nvs_key, (int32_t)value);
        }
    } break;
    case NVS_TYPE_U64: {
        uint64_t value = (uint64_t)g_transfer_state.nvs_transfer_buffer[0] |
                         ((uint64_t)g_transfer_state.nvs_transfer_buffer[1] << 8) |
                         ((uint64_t)g_transfer_state.nvs_transfer_buffer[2] << 16) |
                         ((uint64_t)g_transfer_state.nvs_transfer_buffer[3] << 24) |
                         ((uint64_t)g_transfer_state.nvs_transfer_buffer[4] << 32) |
                         ((uint64_t)g_transfer_state.nvs_transfer_buffer[5] << 40) |
                         ((uint64_t)g_transfer_state.nvs_transfer_buffer[6] << 48) |
                         ((uint64_t)g_transfer_state.nvs_transfer_buffer[7] << 56);
        err = nvs_set_u64(nvs_handle, g_transfer_state.nvs_key, value);
    } break;
    case NVS_TYPE_I64: {
        uint32_t low, high;
        if (!bu_read_u32_le(
                g_transfer_state.nvs_transfer_buffer, NVS_TRANSFER_BUFFER_SIZE, &low) ||
            !bu_read_u32_le(&g_transfer_state.nvs_transfer_buffer[4],
                            NVS_TRANSFER_BUFFER_SIZE - 4,
                            &high)) {
            ESP_LOGE(TAG, "Failed to read i64 value");
            err = ESP_FAIL;
        } else {
            int64_t value = (int64_t)low | ((int64_t)high << 32);
            err = nvs_set_i64(nvs_handle, g_transfer_state.nvs_key, value);
        }
    } break;
    case NVS_TYPE_STR:
        err = nvs_set_str(nvs_handle,
                          g_transfer_state.nvs_key,
                          (const char *)g_transfer_state.nvs_transfer_buffer);
        break;
    case NVS_TYPE_BLOB:
        err = nvs_set_blob(nvs_handle,
                           g_transfer_state.nvs_key,
                           g_transfer_state.nvs_transfer_buffer,
                           g_transfer_state.nvs_value_length);
        break;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set NVS value: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        send_response(RESP_ERROR);
        return;
    }

    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        send_response(RESP_ERROR);
        return;
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "NVS set completed: key='%s'", g_transfer_state.nvs_key);
    g_transfer_state.state = TRANSFER_STATE_IDLE;
    send_response(RESP_OK);
}

// Handle CMD_NVS_GET_START command
static void handle_nvs_get_start(const uint8_t *data) {
    // Reset NVS transfer buffer for new operation
    reset_nvs_transfer_buffer();

    // Copy key (null-terminated, max 15 chars)
    strncpy(g_transfer_state.nvs_key, (const char *)&data[4], 15);
    g_transfer_state.nvs_key[15] = '\0';

    // Validate key length
    size_t key_len = strlen(g_transfer_state.nvs_key);
    if (key_len == 0 || key_len > 15) {
        ESP_LOGE(TAG, "Invalid key length: %lu", (unsigned long)key_len);
        send_response(RESP_ERROR);
        return;
    }

    // Open NVS namespace
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("odkey", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        send_response(RESP_ERROR);
        return;
    }

    // Get key type using nvs_find_key
    nvs_type_t nvs_type;
    err = nvs_find_key(nvs_handle, g_transfer_state.nvs_key, &nvs_type);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to find NVS key: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        send_response(RESP_ERROR);
        return;
    }

    // Store the NVS type directly (no conversion needed)
    g_transfer_state.nvs_value_type = nvs_type;

    // Read value based on type
    err = ESP_FAIL;
    size_t value_size = 0;
    switch (g_transfer_state.nvs_value_type) {
    case NVS_TYPE_U8: {
        uint8_t value;
        err = nvs_get_u8(nvs_handle, g_transfer_state.nvs_key, &value);
        if (err == ESP_OK) {
            g_transfer_state.nvs_transfer_buffer[0] = value;
            value_size = 1;
        }
    } break;
    case NVS_TYPE_I8: {
        int8_t value;
        err = nvs_get_i8(nvs_handle, g_transfer_state.nvs_key, &value);
        if (err == ESP_OK) {
            g_transfer_state.nvs_transfer_buffer[0] = (uint8_t)value;
            value_size = 1;
        }
    } break;
    case NVS_TYPE_U16: {
        uint16_t value;
        err = nvs_get_u16(nvs_handle, g_transfer_state.nvs_key, &value);
        if (err == ESP_OK) {
            g_transfer_state.nvs_transfer_buffer[0] = value & 0xFF;
            g_transfer_state.nvs_transfer_buffer[1] = (value >> 8) & 0xFF;
            value_size = 2;
        }
    } break;
    case NVS_TYPE_I16: {
        int16_t value;
        err = nvs_get_i16(nvs_handle, g_transfer_state.nvs_key, &value);
        if (err == ESP_OK) {
            g_transfer_state.nvs_transfer_buffer[0] = value & 0xFF;
            g_transfer_state.nvs_transfer_buffer[1] = (value >> 8) & 0xFF;
            value_size = 2;
        }
    } break;
    case NVS_TYPE_U32: {
        uint32_t value;
        err = nvs_get_u32(nvs_handle, g_transfer_state.nvs_key, &value);
        if (err == ESP_OK) {
            g_transfer_state.nvs_transfer_buffer[0] = value & 0xFF;
            g_transfer_state.nvs_transfer_buffer[1] = (value >> 8) & 0xFF;
            g_transfer_state.nvs_transfer_buffer[2] = (value >> 16) & 0xFF;
            g_transfer_state.nvs_transfer_buffer[3] = (value >> 24) & 0xFF;
            value_size = 4;
        }
    } break;
    case NVS_TYPE_I32: {
        int32_t value;
        err = nvs_get_i32(nvs_handle, g_transfer_state.nvs_key, &value);
        if (err == ESP_OK) {
            g_transfer_state.nvs_transfer_buffer[0] = value & 0xFF;
            g_transfer_state.nvs_transfer_buffer[1] = (value >> 8) & 0xFF;
            g_transfer_state.nvs_transfer_buffer[2] = (value >> 16) & 0xFF;
            g_transfer_state.nvs_transfer_buffer[3] = (value >> 24) & 0xFF;
            value_size = 4;
        }
    } break;
    case NVS_TYPE_U64: {
        uint64_t value;
        err = nvs_get_u64(nvs_handle, g_transfer_state.nvs_key, &value);
        if (err == ESP_OK) {
            for (int i = 0; i < 8; i++) {
                g_transfer_state.nvs_transfer_buffer[i] = (value >> (i * 8)) & 0xFF;
            }
            value_size = 8;
        }
    } break;
    case NVS_TYPE_I64: {
        int64_t value;
        err = nvs_get_i64(nvs_handle, g_transfer_state.nvs_key, &value);
        if (err == ESP_OK) {
            for (int i = 0; i < 8; i++) {
                g_transfer_state.nvs_transfer_buffer[i] = (value >> (i * 8)) & 0xFF;
            }
            value_size = 8;
        }
    } break;
    case NVS_TYPE_STR: {
        size_t required_size = sizeof(g_transfer_state.nvs_transfer_buffer);
        err = nvs_get_str(nvs_handle,
                          g_transfer_state.nvs_key,
                          (char *)g_transfer_state.nvs_transfer_buffer,
                          &required_size);
        if (err == ESP_OK) {
            value_size = required_size - 1;  // Exclude null terminator
        }
    } break;
    case NVS_TYPE_BLOB: {
        size_t required_size = sizeof(g_transfer_state.nvs_transfer_buffer);
        err = nvs_get_blob(nvs_handle,
                           g_transfer_state.nvs_key,
                           g_transfer_state.nvs_transfer_buffer,
                           &required_size);
        if (err == ESP_OK) {
            value_size = required_size;
        }
    } break;
    }

    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get NVS value: %s", esp_err_to_name(err));
        send_response(RESP_ERROR);
        return;
    }

    // Initialize get state
    g_transfer_state.nvs_value_length = value_size;
    g_transfer_state.state = TRANSFER_STATE_NVS_GETTING;

    ESP_LOGI(TAG,
             "NVS get started: key='%s', type=0x%02X, length=%lu",
             g_transfer_state.nvs_key,
             g_transfer_state.nvs_value_type,
             (unsigned long)value_size);

    // Send first chunk with type and size
    uint8_t response_data[60] = {0};
    response_data[0] = g_transfer_state.nvs_value_type;
    response_data[1] = value_size & 0xFF;
    response_data[2] = (value_size >> 8) & 0xFF;
    response_data[3] = (value_size >> 16) & 0xFF;
    response_data[4] = (value_size >> 24) & 0xFF;

    // Copy first 55 bytes of value data
    size_t first_chunk_size = (value_size > 55) ? 55 : value_size;
    if (first_chunk_size > 0) {
        memcpy(
            &response_data[5], g_transfer_state.nvs_transfer_buffer, first_chunk_size);
        g_transfer_state.nvs_transfer_buffer_transferred = first_chunk_size;
    }

    send_response_with_data(RESP_OK, response_data, 60);
}

// Handle CMD_NVS_GET_DATA command
static void handle_nvs_get_data(void) {
    if (g_transfer_state.state != TRANSFER_STATE_NVS_GETTING) {
        ESP_LOGE(TAG, "NVS_GET_DATA received but not in getting state");
        send_response(RESP_ERROR);
        return;
    }

    if (g_transfer_state.nvs_transfer_buffer_transferred >=
        g_transfer_state.nvs_value_length) {
        ESP_LOGE(TAG, "All NVS data already sent");
        g_transfer_state.state = TRANSFER_STATE_ERROR;
        send_response(RESP_ERROR);
        return;
    }

    // Calculate how many bytes to send (60 bytes max)
    size_t bytes_remaining = g_transfer_state.nvs_value_length -
                             g_transfer_state.nvs_transfer_buffer_transferred;
    size_t bytes_to_send = (bytes_remaining > 60) ? 60 : bytes_remaining;

    // Send chunk
    send_response_with_data(
        RESP_OK,
        &g_transfer_state
             .nvs_transfer_buffer[g_transfer_state.nvs_transfer_buffer_transferred],
        bytes_to_send);

    g_transfer_state.nvs_transfer_buffer_transferred += bytes_to_send;

    // If we've sent all data, reset state
    if (g_transfer_state.nvs_transfer_buffer_transferred >=
        g_transfer_state.nvs_value_length) {
        ESP_LOGI(TAG, "NVS get completed: key='%s'", g_transfer_state.nvs_key);
        g_transfer_state.state = TRANSFER_STATE_IDLE;
    }
}

// Handle CMD_NVS_DELETE command
static void handle_nvs_delete(const uint8_t *data) {
    // Copy key (null-terminated, max 15 chars)
    strncpy(g_transfer_state.nvs_key, (const char *)&data[4], 15);
    g_transfer_state.nvs_key[15] = '\0';

    // Validate key length
    size_t key_len = strlen(g_transfer_state.nvs_key);
    if (key_len == 0 || key_len > 15) {
        ESP_LOGE(TAG, "Invalid key length: %lu", (unsigned long)key_len);
        send_response(RESP_ERROR);
        return;
    }

    // Open NVS namespace
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("odkey", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        send_response(RESP_ERROR);
        return;
    }

    // Delete key
    err = nvs_erase_key(nvs_handle, g_transfer_state.nvs_key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase NVS key: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        send_response(RESP_ERROR);
        return;
    }

    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        send_response(RESP_ERROR);
        return;
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "NVS delete completed: key='%s'", g_transfer_state.nvs_key);
    send_response(RESP_OK);
}

// Handle CMD_LOG_READ_START command
static void handle_log_read_start(void) {
    // Start reading from the beginning of the buffer
    log_buffer_start_read();

    g_transfer_state.state = TRANSFER_STATE_LOG_STREAMING;

    send_response(RESP_OK);
}

// Handle CMD_LOG_READ_CHUNK command (host requests log data)
static void handle_log_read_chunk(void) {
    if (g_transfer_state.state != TRANSFER_STATE_LOG_STREAMING) {
        send_response(RESP_ERROR);
        return;
    }

    uint8_t chunk_buffer[60] = {0};
    uint32_t bytes_read = log_buffer_read_chunk(chunk_buffer, sizeof(chunk_buffer));

    if (bytes_read > 0) {
        send_response_with_data(RESP_OK, chunk_buffer, bytes_read);
    } else {
        // No more data, send zero-byte chunk to indicate end
        send_response_with_data(RESP_OK, NULL, 0);
        g_transfer_state.state = TRANSFER_STATE_IDLE;
    }
}

// Handle CMD_LOG_READ_STOP command
static void handle_log_read_stop(void) {
    g_transfer_state.state = TRANSFER_STATE_IDLE;
    send_response(RESP_OK);
}

// Handle CMD_LOG_CLEAR command
static void handle_log_clear(void) {
    log_buffer_clear();
    send_response(RESP_OK);
}

void usb_system_config_process_command(const uint8_t *data, uint16_t len) {
    // Validate input
    if (data == NULL || len == 0 || len > 64) {
        ESP_LOGE(TAG, "Invalid command data: data=%p, len=%d", data, len);
        return;
    }

    // Enqueue command for processing by the command task
    command_queue_item_t cmd_item;
    memset(&cmd_item, 0, sizeof(cmd_item));
    memcpy(cmd_item.data, data, len);
    cmd_item.len = len;

    if (xQueueSend(g_command_queue, &cmd_item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Command queue full, dropping command 0x%02X", data[0]);
    }
}

static void process_command_internal(const uint8_t *data, uint16_t len) {
    // Every command must have at least the command code (4 bytes)
    if (data == NULL || len < 4) {
        ESP_LOGE(TAG, "Invalid command data");
        send_response(RESP_ERROR);
        return;
    }

    // Debug: Print what we received
    ESP_LOGD(
        TAG,
        "Processing command: len=%d, data[0]=0x%02X, first 8 bytes: %02X %02X %02X "
        "%02X %02X %02X %02X %02X",
        len,
        data[0],
        data[0],
        data[1],
        data[2],
        data[3],
        data[4],
        data[5],
        data[6],
        data[7]);

    uint8_t command = data[0];  // Command code is currently only on the first byte

    switch (command) {
    case CMD_FLASH_PROGRAM_WRITE_START:
        if (len < 8)  // Need command code + 4 bytes for program size
        {
            ESP_LOGE(TAG, "PROGRAM_WRITE_START command too short");
            send_response(RESP_ERROR);
            return;
        } else {
            uint32_t program_size;
            if (!bu_read_u32_le(&data[4], len - 4, &program_size)) {
                ESP_LOGE(TAG, "Failed to read program size");
                send_response(RESP_ERROR);
                return;
            }
            handle_flash_program_write_start(program_size);
        }
        break;

    case CMD_FLASH_PROGRAM_WRITE_CHUNK:
        if (len != 64)  // Must be exactly 64 bytes
        {
            ESP_LOGE(TAG, "PROGRAM_WRITE_CHUNK must be exactly 64 bytes, got %d", len);
            send_response(RESP_ERROR);
            return;
        } else {
            const uint8_t *chunk_data = &data[4];  // Data payload is in bytes 4-63
            handle_flash_program_write_chunk(chunk_data, 60);
        }
        break;

    case CMD_FLASH_PROGRAM_WRITE_FINISH:
        if (len < 8)  // Need command code + 4 bytes for program size
        {
            ESP_LOGE(TAG, "PROGRAM_WRITE_FINISH command too short");
            send_response(RESP_ERROR);
            return;
        } else {
            uint32_t program_size;
            if (!bu_read_u32_le(&data[4], len - 4, &program_size)) {
                ESP_LOGE(TAG, "Failed to read program size");
                send_response(RESP_ERROR);
                return;
            }
            handle_flash_program_write_finish(program_size);
        }
        break;

    case CMD_FLASH_PROGRAM_READ_START:
        handle_flash_program_read_start();
        break;

    case CMD_FLASH_PROGRAM_READ_CHUNK:
        handle_program_read_chunk();
        break;

    case CMD_FLASH_PROGRAM_EXECUTE:
        handle_flash_program_execute();
        break;

    case CMD_RAM_PROGRAM_WRITE_START:
        if (len < 8)  // Need command code + 4 bytes for program size
        {
            ESP_LOGE(TAG, "RAM_PROGRAM_WRITE_START command too short");
            send_response(RESP_ERROR);
            return;
        } else {
            uint32_t program_size;
            if (!bu_read_u32_le(&data[4], len - 4, &program_size)) {
                ESP_LOGE(TAG, "Failed to read program size");
                send_response(RESP_ERROR);
                return;
            }
            handle_ram_program_write_start(program_size);
        }
        break;

    case CMD_RAM_PROGRAM_WRITE_CHUNK:
        if (len != 64)  // Must be exactly 64 bytes
        {
            ESP_LOGE(
                TAG, "RAM_PROGRAM_WRITE_CHUNK must be exactly 64 bytes, got %d", len);
            send_response(RESP_ERROR);
            return;
        } else {
            const uint8_t *chunk_data = &data[4];  // Data payload is in bytes 4-63
            handle_ram_program_write_chunk(chunk_data, 60);
        }
        break;

    case CMD_RAM_PROGRAM_WRITE_FINISH:
        if (len < 8)  // Need command code + 4 bytes for program size
        {
            ESP_LOGE(TAG, "RAM_PROGRAM_WRITE_FINISH command too short");
            send_response(RESP_ERROR);
            return;
        } else {
            uint32_t program_size;
            if (!bu_read_u32_le(&data[4], len - 4, &program_size)) {
                ESP_LOGE(TAG, "Failed to read program size");
                send_response(RESP_ERROR);
                return;
            }
            handle_ram_program_write_finish(program_size);
        }
        break;

    case CMD_RAM_PROGRAM_EXECUTE:
        handle_ram_program_execute();
        break;

    case CMD_RAM_PROGRAM_READ_START:
        handle_ram_program_read_start();
        break;

    case CMD_RAM_PROGRAM_READ_CHUNK:
        handle_program_read_chunk();  // Reuse same logic for chunk reading
        break;

    case CMD_NVS_SET_START:
        if (len < 25)  // Need command code + type(1) + length(4) + key(16) = 25 bytes
        {
            ESP_LOGE(TAG, "NVS_SET_START command too short");
            send_response(RESP_ERROR);
            return;
        } else {
            handle_nvs_set_start(data);
        }
        break;

    case CMD_NVS_SET_DATA:
        handle_nvs_set_data(data);
        break;

    case CMD_NVS_SET_FINISH:
        handle_nvs_set_finish();
        break;

    case CMD_NVS_GET_START:
        if (len < 20)  // Need command code + key(16) = 20 bytes
        {
            ESP_LOGE(TAG, "NVS_GET_START command too short");
            send_response(RESP_ERROR);
            return;
        } else {
            handle_nvs_get_start(data);
        }
        break;

    case CMD_NVS_GET_DATA:
        handle_nvs_get_data();
        break;

    case CMD_NVS_DELETE:
        if (len < 20)  // Need command code + key(16) = 20 bytes
        {
            ESP_LOGE(TAG, "NVS_DELETE command too short");
            send_response(RESP_ERROR);
            return;
        } else {
            handle_nvs_delete(data);
        }
        break;

    case CMD_LOG_READ_START:
        handle_log_read_start();
        break;

    case CMD_LOG_READ_CHUNK:
        handle_log_read_chunk();
        break;

    case CMD_LOG_READ_STOP:
        handle_log_read_stop();
        break;

    case CMD_LOG_CLEAR:
        handle_log_clear();
        break;

    default:
        ESP_LOGW(TAG, "Unknown command: 0x%02X", command);
        send_response(RESP_ERROR);
        break;
    }
}
