#include "program.h"
#include "esp_log.h"
#include "program_flash.h"
#include "program_ram.h"
#include "vm_task.h"

static const char *TAG = "program";

// Store the external HID callback
static program_hid_send_callback_t g_external_hid_callback = NULL;

// Private callback that forwards to the external callback
static bool program_hid_send_callback(uint8_t modifier,
                                      const uint8_t *keys,
                                      uint8_t count) {
    if (g_external_hid_callback != NULL) {
        return g_external_hid_callback(modifier, keys, count);
    }
    return false;
}

bool program_init(program_hid_send_callback_t hid_send_callback) {
    // Store the external callback
    g_external_hid_callback = hid_send_callback;

    // Initialize flash program
    if (!program_flash_init()) {
        ESP_LOGE(TAG, "Failed to initialize flash program");
        return false;
    }

    // Initialize RAM program
    if (!program_ram_init()) {
        ESP_LOGE(TAG, "Failed to initialize RAM program");
        return false;
    }

    // Initialize VM task with our private callback
    if (!vm_task_init(program_hid_send_callback)) {
        ESP_LOGE(TAG, "Failed to initialize VM task");
        return false;
    }

    ESP_LOGI(TAG, "Program initialized");
    return true;
}

const uint8_t *program_get(program_type_t type, uint32_t *out_size) {
    if (out_size == NULL) {
        ESP_LOGE(TAG, "out_size parameter cannot be NULL");
        return NULL;
    }

    switch (type) {
    case PROGRAM_TYPE_FLASH:
        return program_flash_get(out_size);

    case PROGRAM_TYPE_RAM:
        return program_ram_get(out_size);

    default:
        ESP_LOGE(TAG, "Invalid program type: %d", type);
        *out_size = 0;
        return NULL;
    }
}

bool program_write_start(program_type_t type,
                         uint32_t expected_program_size,
                         program_write_source_t source) {
    // Auto-halt VM if running
    if (vm_task_is_running()) {
        ESP_LOGI(TAG, "Halting VM for program upload");
        vm_task_halt();
    }

    switch (type) {
    case PROGRAM_TYPE_FLASH:
        return program_flash_write_start(expected_program_size, source);

    case PROGRAM_TYPE_RAM:
        return program_ram_write_start(expected_program_size, source);

    default:
        ESP_LOGE(TAG, "Invalid program type: %d", type);
        return false;
    }
}

bool program_write_chunk(program_type_t type,
                         const uint8_t *data,
                         uint32_t size,
                         program_write_source_t source) {
    switch (type) {
    case PROGRAM_TYPE_FLASH:
        return program_flash_write_chunk(data, size, source);

    case PROGRAM_TYPE_RAM:
        return program_ram_write_chunk(data, size, source);

    default:
        ESP_LOGE(TAG, "Invalid program type: %d", type);
        return false;
    }
}

bool program_write_finish(program_type_t type,
                          uint32_t program_size,
                          program_write_source_t source) {
    switch (type) {
    case PROGRAM_TYPE_FLASH:
        return program_flash_write_finish(program_size, source);

    case PROGRAM_TYPE_RAM:
        return program_ram_write_finish(program_size, source);

    default:
        ESP_LOGE(TAG, "Invalid program type: %d", type);
        return false;
    }
}

bool program_erase(program_type_t type) {
    switch (type) {
    case PROGRAM_TYPE_FLASH:
        return program_flash_erase();

    case PROGRAM_TYPE_RAM:
        return program_ram_erase();

    default:
        ESP_LOGE(TAG, "Invalid program type: %d", type);
        return false;
    }
}

uint32_t program_get_bytes_written(program_type_t type) {
    switch (type) {
    case PROGRAM_TYPE_FLASH:
        return program_flash_get_bytes_written();

    case PROGRAM_TYPE_RAM:
        return program_ram_get_bytes_written();

    default:
        ESP_LOGE(TAG, "Invalid program type: %d", type);
        return 0;
    }
}

uint32_t program_get_expected_size(program_type_t type) {
    switch (type) {
    case PROGRAM_TYPE_FLASH:
        return program_flash_get_expected_size();

    case PROGRAM_TYPE_RAM:
        return program_ram_get_expected_size();

    default:
        ESP_LOGE(TAG, "Invalid program type: %d", type);
        return 0;
    }
}

bool program_execute(program_type_t type,
                     program_execution_complete_callback_t on_complete,
                     void *on_complete_arg) {
    if (vm_task_is_running()) {
        ESP_LOGE(TAG, "Program already running");
        return false;
    }

    // Load program from storage
    uint32_t program_size;
    const uint8_t *program = program_get(type, &program_size);

    if (program == NULL || program_size == 0) {
        ESP_LOGI(TAG, "No valid program in storage for type %d", type);
        return false;
    }

    ESP_LOGI(TAG, "Loaded program (%lu bytes)", (unsigned long)program_size);

    // Start program execution with completion callback
    if (!vm_task_start_program(program, program_size, on_complete, on_complete_arg)) {
        ESP_LOGW(TAG, "Failed to start program execution");
        return false;
    }

    ESP_LOGI(TAG, "Program execution started");
    return true;
}

bool program_is_running(void) {
    return vm_task_is_running();
}

bool program_halt(void) {
    return vm_task_halt();
}
