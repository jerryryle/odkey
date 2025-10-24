#include "odkeyscript_vm.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb_keyboard.h"
#include "usb_keyboard_keys.h"

static const char *TAG = "odkeyscript_vm";

// Opcode definitions (matching the Python compiler)
#define OPCODE_KEYDN 0x10
#define OPCODE_KEYUP 0x11
#define OPCODE_KEYUP_ALL 0x12
#define OPCODE_WAIT 0x13
#define OPCODE_SET_COUNTER 0x14
#define OPCODE_DEC 0x15
#define OPCODE_JNZ 0x16

// Helper function to send HID report using callback
static bool vm_send_hid_report(vm_context_t *ctx,
                               uint8_t modifier,
                               const uint8_t *keys,
                               uint8_t count) {
    if (ctx->hid_callback) {
        return ctx->hid_callback(modifier, keys, count);
    }
    return false;
}

// Helper function to sleep for specified milliseconds using callback
static void vm_sleep_ms(vm_context_t *ctx, uint16_t ms) {
    if (ctx->delay_callback) {
        ctx->delay_callback(ms);
    }
}

// Helper function to read 8-bit value from program memory with bounds checking
static bool vm_read_u8(vm_context_t *ctx, uint8_t *value) {
    if (ctx->pc >= ctx->program_size) {
        return false;
    }
    *value = ctx->program[ctx->pc++];
    return true;
}

// Helper function to read 16-bit value from program memory with bounds checking
static bool vm_read_u16(vm_context_t *ctx, uint16_t *value) {
    if (ctx->pc + 1 >= ctx->program_size) {
        return false;
    }
    *value =
        (uint16_t)ctx->program[ctx->pc] | ((uint16_t)ctx->program[ctx->pc + 1] << 8);
    ctx->pc += 2;
    return true;
}

// Helper function to read 32-bit value from program memory with bounds checking
static bool vm_read_u32(vm_context_t *ctx, uint32_t *value) {
    if (ctx->pc + 3 >= ctx->program_size) {
        return false;
    }
    *value = (uint32_t)ctx->program[ctx->pc] |
             ((uint32_t)ctx->program[ctx->pc + 1] << 8) |
             ((uint32_t)ctx->program[ctx->pc + 2] << 16) |
             ((uint32_t)ctx->program[ctx->pc + 3] << 24);
    ctx->pc += 4;
    return true;
}

// Helper function to read array of bytes from program memory with bounds checking
static bool vm_read_bytes(vm_context_t *ctx, uint8_t *buffer, size_t count) {
    if (ctx->pc + count > ctx->program_size) {
        return false;
    }
    memcpy(buffer, &ctx->program[ctx->pc], count);
    ctx->pc += count;
    return true;
}

// Helper function to clear the zero flag (called by all opcodes except DEC)
static void vm_clear_zero_flag(vm_context_t *ctx) {
    ctx->zero_flag = false;
}

// Helper function to release all currently pressed keys
static void vm_release_all_keys(vm_context_t *ctx) {
    if (ctx->current_key_count > 0 || ctx->current_modifier != 0) {
        ESP_LOGD(TAG,
                 "Releasing all keys (modifier: 0x%02X, keys: %d)",
                 ctx->current_modifier,
                 ctx->current_key_count);

        vm_send_hid_report(ctx, 0, NULL, 0);
        ctx->current_modifier = 0;
        ctx->current_key_count = 0;
        memset(ctx->current_keys, 0, sizeof(ctx->current_keys));
        ctx->keys_released++;
    }
}

bool vm_init(vm_context_t *ctx) {
    if (ctx == NULL) {
        return false;
    }

    memset(ctx, 0, sizeof(vm_context_t));
    ctx->state = VM_STATE_READY;
    ctx->current_press_time = 50;  // Default 50ms press time
    ctx->zero_flag = false;
    ctx->hid_callback = NULL;
    ctx->delay_callback = NULL;

    ESP_LOGD(TAG, "VM initialized");
    return true;
}

void vm_reset(vm_context_t *ctx) {
    if (ctx == NULL) {
        return;
    }

    // Release any pressed keys before resetting
    vm_release_all_keys(ctx);

    // Save callbacks before reset
    vm_hid_callback_t hid_cb = ctx->hid_callback;
    vm_delay_callback_t delay_cb = ctx->delay_callback;

    // Reset all state
    memset(ctx, 0, sizeof(vm_context_t));
    ctx->state = VM_STATE_READY;
    ctx->current_press_time = 50;  // Default 50ms press time
    ctx->zero_flag = false;
    ctx->hid_callback = hid_cb;
    ctx->delay_callback = delay_cb;

    ESP_LOGD(TAG, "VM reset");
}

vm_error_t vm_start(vm_context_t *ctx,
                    const uint8_t *program,
                    uint32_t program_size,
                    vm_hid_callback_t hid_callback,
                    vm_delay_callback_t delay_callback) {
    if (ctx == NULL || program == NULL || program_size == 0 || hid_callback == NULL ||
        delay_callback == NULL) {
        return VM_ERROR_INVALID_PROGRAM;
    }

    // Initialize VM state
    vm_reset(ctx);
    ctx->program = program;
    ctx->program_size = program_size;
    ctx->pc = 0;
    ctx->state = VM_STATE_RUNNING;
    ctx->hid_callback = hid_callback;
    ctx->delay_callback = delay_callback;

    ESP_LOGI(TAG,
             "Starting VM execution (program size: %lu bytes)",
             (unsigned long)program_size);
    return VM_ERROR_NONE;
}

vm_error_t vm_step(vm_context_t *ctx) {
    if (ctx == NULL || ctx->state != VM_STATE_RUNNING) {
        return VM_ERROR_INVALID_PROGRAM;
    }

    // Check if we've reached the end of the program
    if (ctx->pc >= ctx->program_size) {
        vm_release_all_keys(ctx);
        ctx->state = VM_STATE_FINISHED;
        ESP_LOGI(TAG, "Program completed successfully");
        return VM_ERROR_NONE;
    }

    // Execute next opcode
    uint8_t opcode = ctx->program[ctx->pc];
    ctx->pc++;
    ctx->instructions_executed++;

    ESP_LOGD(
        TAG, "Executing opcode 0x%02X at PC %lu", opcode, (unsigned long)(ctx->pc - 1));

    switch (opcode) {
    case OPCODE_KEYDN: {
        // KEYDN modifier keycount key1 key2 ... keyN
        uint8_t modifier, key_count;
        if (!vm_read_u8(ctx, &modifier) || !vm_read_u8(ctx, &key_count)) {
            ctx->error = VM_ERROR_INVALID_ADDRESS;
            ctx->state = VM_STATE_ERROR;
            break;
        }

        if (key_count > VM_MAX_KEYS_PRESSED) {
            ctx->error = VM_ERROR_INVALID_OPERAND;
            ctx->state = VM_STATE_ERROR;
            break;
        }

        // Read keycodes
        uint8_t keys[VM_MAX_KEYS_PRESSED];
        if (!vm_read_bytes(ctx, keys, key_count)) {
            ctx->error = VM_ERROR_INVALID_ADDRESS;
            ctx->state = VM_STATE_ERROR;
            break;
        }

        // Update current state
        ctx->current_modifier = modifier;
        ctx->current_key_count = key_count;
        memset(ctx->current_keys, 0, sizeof(ctx->current_keys));
        if (key_count > 0) {
            memcpy(ctx->current_keys, keys, key_count);
        }

        // Send HID report
        if (!vm_send_hid_report(ctx,
                                ctx->current_modifier,
                                ctx->current_keys,
                                ctx->current_key_count)) {
            ctx->error = VM_ERROR_HID_ERROR;
            ctx->state = VM_STATE_ERROR;
            break;
        }

        vm_clear_zero_flag(ctx);
        ctx->keys_pressed++;

        ESP_LOGD(TAG, "KEYDN: modifier=0x%02X, keys=%d", modifier, key_count);
        break;
    }

    case OPCODE_KEYUP: {
        // KEYUP modifier keycount key1 key2 ... keyN
        uint8_t modifier, key_count;
        if (!vm_read_u8(ctx, &modifier) || !vm_read_u8(ctx, &key_count)) {
            ctx->error = VM_ERROR_INVALID_ADDRESS;
            ctx->state = VM_STATE_ERROR;
            break;
        }

        if (key_count > VM_MAX_KEYS_PRESSED) {
            ctx->error = VM_ERROR_INVALID_OPERAND;
            ctx->state = VM_STATE_ERROR;
            break;
        }

        // Read keycodes
        uint8_t keys[VM_MAX_KEYS_PRESSED];
        if (!vm_read_bytes(ctx, keys, key_count)) {
            ctx->error = VM_ERROR_INVALID_ADDRESS;
            ctx->state = VM_STATE_ERROR;
            break;
        }

        // Remove keys from current state
        // Remove modifiers
        ctx->current_modifier &= ~modifier;

        // Remove keys from current_keys array
        uint8_t new_keys[VM_MAX_KEYS_PRESSED];
        uint8_t new_key_count = 0;

        // Copy keys that are NOT in the release list
        for (uint8_t i = 0; i < ctx->current_key_count; i++) {
            bool should_keep = true;
            for (uint8_t j = 0; j < key_count; j++) {
                if (ctx->current_keys[i] == keys[j]) {
                    should_keep = false;
                    break;
                }
            }
            if (should_keep) {
                new_keys[new_key_count++] = ctx->current_keys[i];
            }
        }

        // Update current keys with filtered list
        ctx->current_key_count = new_key_count;
        memcpy(ctx->current_keys, new_keys, new_key_count);

        // Send HID report with updated state
        if (!vm_send_hid_report(ctx,
                                ctx->current_modifier,
                                ctx->current_keys,
                                ctx->current_key_count)) {
            ctx->error = VM_ERROR_HID_ERROR;
            ctx->state = VM_STATE_ERROR;
            break;
        }

        vm_clear_zero_flag(ctx);
        ctx->keys_released++;

        ESP_LOGD(TAG, "KEYUP: modifier=0x%02X, keys=%d", modifier, key_count);
        break;
    }

    case OPCODE_KEYUP_ALL: {
        // KEYUP_ALL - release all keys
        vm_release_all_keys(ctx);
        vm_clear_zero_flag(ctx);
        ESP_LOGD(TAG, "KEYUP_ALL: released all keys");
        break;
    }

    case OPCODE_WAIT: {
        // WAIT time_ms
        uint16_t time_ms;
        if (!vm_read_u16(ctx, &time_ms)) {
            ctx->error = VM_ERROR_INVALID_ADDRESS;
            ctx->state = VM_STATE_ERROR;
            break;
        }

        ESP_LOGD(TAG, "WAIT: %d ms", time_ms);
        vm_sleep_ms(ctx, time_ms);
        vm_clear_zero_flag(ctx);
        break;
    }

    case OPCODE_SET_COUNTER: {
        // SET_COUNTER counter_id value
        uint8_t counter_id;
        uint16_t value;
        if (!vm_read_u8(ctx, &counter_id) || !vm_read_u16(ctx, &value)) {
            ctx->error = VM_ERROR_INVALID_ADDRESS;
            ctx->state = VM_STATE_ERROR;
            break;
        }

        if (counter_id >= (VM_MAX_COUNTERS - 1)) {
            ctx->error = VM_ERROR_INVALID_ADDRESS;
            ctx->state = VM_STATE_ERROR;
            break;
        }

        ctx->counters[counter_id] = value;
        vm_clear_zero_flag(ctx);
        ESP_LOGD(TAG, "SET_COUNTER: counter[%d] = %d", counter_id, value);
        break;
    }

    case OPCODE_DEC: {
        // DEC counter_id
        uint8_t counter_id;
        if (!vm_read_u8(ctx, &counter_id)) {
            ctx->error = VM_ERROR_INVALID_ADDRESS;
            ctx->state = VM_STATE_ERROR;
            break;
        }

        if (counter_id >= (VM_MAX_COUNTERS - 1)) {
            ctx->error = VM_ERROR_INVALID_ADDRESS;
            ctx->state = VM_STATE_ERROR;
            break;
        }

        if (ctx->counters[counter_id] > 0) {
            ctx->counters[counter_id]--;
        }
        ctx->zero_flag =
            (ctx->counters[counter_id] == 0);  // Set flag if decrement resulted in zero

        ESP_LOGD(TAG,
                 "DEC: counter[%d] = %d, zero_flag = %s",
                 counter_id,
                 ctx->counters[counter_id],
                 ctx->zero_flag ? "true" : "false");
        break;
    }

    case OPCODE_JNZ: {
        // JNZ address - jump if zero flag is NOT set
        uint32_t address;
        if (!vm_read_u32(ctx, &address)) {
            ctx->error = VM_ERROR_INVALID_ADDRESS;
            ctx->state = VM_STATE_ERROR;
            break;
        }

        if (address >= ctx->program_size) {
            ctx->error = VM_ERROR_INVALID_ADDRESS;
            ctx->state = VM_STATE_ERROR;
            break;
        }

        if (!ctx->zero_flag) {
            ctx->pc = address;
            ESP_LOGD(TAG, "JNZ: zero_flag=false, jumping to %lu", address);
        } else {
            ESP_LOGD(TAG, "JNZ: zero_flag=true, not jumping");
        }
        vm_clear_zero_flag(ctx);
        break;
    }

    default: {
        ESP_LOGE(TAG,
                 "Invalid opcode: 0x%02X at PC %lu",
                 opcode,
                 (unsigned long)(ctx->pc - 1));
        ctx->error = VM_ERROR_INVALID_OPCODE;
        ctx->state = VM_STATE_ERROR;
        break;
    }
    }

    // Handle errors
    if (ctx->state == VM_STATE_ERROR) {
        vm_release_all_keys(ctx);
        ESP_LOGE(TAG, "Program failed with error: %s", vm_error_to_string(ctx->error));
    }

    return ctx->error;
}

bool vm_running(const vm_context_t *ctx) {
    return ctx != NULL && ctx->state == VM_STATE_RUNNING;
}

const char *vm_error_to_string(vm_error_t error) {
    switch (error) {
    case VM_ERROR_NONE:
        return "No error";
    case VM_ERROR_INVALID_OPCODE:
        return "Invalid opcode";
    case VM_ERROR_INVALID_OPERAND:
        return "Invalid operand";
    case VM_ERROR_INVALID_ADDRESS:
        return "Invalid address";
    case VM_ERROR_HID_ERROR:
        return "HID error";
    case VM_ERROR_INVALID_PROGRAM:
        return "Invalid program";
    default:
        return "Unknown error";
    }
}

const char *vm_state_to_string(vm_state_t state) {
    switch (state) {
    case VM_STATE_READY:
        return "Ready";
    case VM_STATE_RUNNING:
        return "Running";
    case VM_STATE_PAUSED:
        return "Paused";
    case VM_STATE_ERROR:
        return "Error";
    case VM_STATE_FINISHED:
        return "Finished";
    default:
        return "Unknown";
    }
}

bool vm_has_error(const vm_context_t *ctx) {
    return ctx != NULL && ctx->state == VM_STATE_ERROR;
}

void vm_get_stats(const vm_context_t *ctx,
                  uint32_t *instructions_executed,
                  uint32_t *keys_pressed,
                  uint32_t *keys_released) {
    if (ctx == NULL) {
        return;
    }

    if (instructions_executed)
        *instructions_executed = ctx->instructions_executed;
    if (keys_pressed)
        *keys_pressed = ctx->keys_pressed;
    if (keys_released)
        *keys_released = ctx->keys_released;
}
