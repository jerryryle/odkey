#include "odkeyscript_vm.h"
#include "usb_keyboard.h"
#include "usb_keyboard_keys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "odkeyscript_vm";

// Opcode definitions (matching the Python compiler)
#define OPCODE_KEYDN        0x10
#define OPCODE_KEYUP        0x11
#define OPCODE_KEYUP_ALL    0x12
#define OPCODE_WAIT         0x13
#define OPCODE_SET_COUNTER  0x14
#define OPCODE_DEC          0x15
#define OPCODE_JNZ          0x16

// Helper function to send HID report
static bool vm_send_hid_report(uint8_t modifier, const uint8_t* keys, uint8_t count) {
    return usb_keyboard_send_keys(modifier, keys, count);
}

// Helper function to sleep for specified milliseconds
static void vm_sleep_ms(uint16_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

// Helper function to read 8-bit value from program memory with bounds checking
static bool vm_read_u8(vm_context_t* ctx, uint8_t* value) {
    if (ctx->pc >= ctx->program_size) {
        return false;
    }
    *value = ctx->program[ctx->pc++];
    return true;
}

// Helper function to read 16-bit value from program memory with bounds checking
static bool vm_read_u16(vm_context_t* ctx, uint16_t* value) {
    if (ctx->pc + 1 >= ctx->program_size) {
        return false;
    }
    *value = (uint16_t)ctx->program[ctx->pc] | ((uint16_t)ctx->program[ctx->pc + 1] << 8);
    ctx->pc += 2;
    return true;
}

// Helper function to read 32-bit value from program memory with bounds checking
static bool vm_read_u32(vm_context_t* ctx, uint32_t* value) {
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
static bool vm_read_bytes(vm_context_t* ctx, uint8_t* buffer, size_t count) {
    if (ctx->pc + count > ctx->program_size) {
        return false;
    }
    memcpy(buffer, &ctx->program[ctx->pc], count);
    ctx->pc += count;
    return true;
}

// Helper function to clear the zero flag (called by all opcodes except DEC)
static void vm_clear_zero_flag(vm_context_t* ctx) {
    ctx->zero_flag = false;
}

// Helper function to release all currently pressed keys
static void vm_release_all_keys(vm_context_t* ctx) {
    if (ctx->current_key_count > 0 || ctx->current_modifier != 0) {
        ESP_LOGD(TAG, "Releasing all keys (modifier: 0x%02X, keys: %d)", 
                 ctx->current_modifier, ctx->current_key_count);
        
        vm_send_hid_report(0, NULL, 0);
        ctx->current_modifier = 0;
        ctx->current_key_count = 0;
        memset(ctx->current_keys, 0, sizeof(ctx->current_keys));
        ctx->keys_released++;
    }
}


bool vm_init(vm_context_t* ctx) {
    if (ctx == NULL) {
        return false;
    }
    
    memset(ctx, 0, sizeof(vm_context_t));
    ctx->state = VM_STATE_READY;
    ctx->current_press_time = 50; // Default 50ms press time
    ctx->zero_flag = false;
    
    ESP_LOGD(TAG, "VM initialized");
    return true;
}

void vm_reset(vm_context_t* ctx) {
    if (ctx == NULL) {
        return;
    }
    
    // Release any pressed keys before resetting
    vm_release_all_keys(ctx);
    
    // Reset all state
    memset(ctx, 0, sizeof(vm_context_t));
    ctx->state = VM_STATE_READY;
    ctx->current_press_time = 50; // Default 50ms press time
    ctx->zero_flag = false;
    
    ESP_LOGD(TAG, "VM reset");
}

vm_error_t vm_run(vm_context_t* ctx, const uint8_t* program, size_t program_size) {
    if (ctx == NULL || program == NULL || program_size == 0) {
        return VM_ERROR_INVALID_PROGRAM;
    }
    
    
    // Initialize VM state
    vm_reset(ctx);
    ctx->program = program;
    ctx->program_size = program_size;
    ctx->pc = 0;
    ctx->state = VM_STATE_RUNNING;
    
    ESP_LOGI(TAG, "Starting VM execution (program size: %zu bytes)", program_size);
    
    // Main execution loop
    while (ctx->state == VM_STATE_RUNNING && ctx->pc < ctx->program_size) {
        uint8_t opcode = program[ctx->pc];
        ctx->pc++;
        ctx->instructions_executed++;
        
        ESP_LOGD(TAG, "Executing opcode 0x%02X at PC %zu", opcode, ctx->pc - 1);
        
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
                
                // Send HID report
                if (!vm_send_hid_report(modifier, keys, key_count)) {
                    ctx->error = VM_ERROR_HID_ERROR;
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
                    ctx->error = VM_ERROR_INVALID_ADDRESS;
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
                
                // Send HID report
                if (!vm_send_hid_report(modifier, keys, key_count)) {
                    ctx->error = VM_ERROR_HID_ERROR;
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
                vm_sleep_ms(time_ms);
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
                
                if (counter_id >= VM_MAX_COUNTERS) {
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
                
                if (counter_id >= VM_MAX_COUNTERS) {
                    ctx->error = VM_ERROR_INVALID_ADDRESS;
                    ctx->state = VM_STATE_ERROR;
                    break;
                }
                
                if (ctx->counters[counter_id] > 0) {
                    ctx->counters[counter_id]--;
                }
                ctx->zero_flag = (ctx->counters[counter_id] == 0); // Set flag if decrement resulted in zero
                
                ESP_LOGD(TAG, "DEC: counter[%d] = %d, zero_flag = %s", 
                         counter_id, ctx->counters[counter_id], ctx->zero_flag ? "true" : "false");
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
                    ESP_LOGD(TAG, "JNZ: zero_flag=false, jumping to %d", address);
                } else {
                    ESP_LOGD(TAG, "JNZ: zero_flag=true, not jumping");
                }
                vm_clear_zero_flag(ctx);
                break;
            }
            
            default: {
                ESP_LOGE(TAG, "Invalid opcode: 0x%02X at PC %zu", opcode, ctx->pc - 1);
                ctx->error = VM_ERROR_INVALID_OPCODE;
                ctx->state = VM_STATE_ERROR;
                break;
            }
        }
    }
    
    // Program finished - release any remaining keys
    if (ctx->state == VM_STATE_RUNNING) {
        vm_release_all_keys(ctx);
        ctx->state = VM_STATE_FINISHED;
        ESP_LOGI(TAG, "Program completed successfully");
    } else if (ctx->state == VM_STATE_ERROR) {
        // Release keys even on error
        vm_release_all_keys(ctx);
        ESP_LOGE(TAG, "Program failed with error: %s", vm_error_to_string(ctx->error));
    }
    
    return ctx->error;
}

const char* vm_error_to_string(vm_error_t error) {
    switch (error) {
        case VM_ERROR_NONE: return "No error";
        case VM_ERROR_INVALID_OPCODE: return "Invalid opcode";
        case VM_ERROR_INVALID_OPERAND: return "Invalid operand";
        case VM_ERROR_INVALID_ADDRESS: return "Invalid address";
        case VM_ERROR_HID_ERROR: return "HID error";
        case VM_ERROR_INVALID_PROGRAM: return "Invalid program";
        default: return "Unknown error";
    }
}

const char* vm_state_to_string(vm_state_t state) {
    switch (state) {
        case VM_STATE_READY: return "Ready";
        case VM_STATE_RUNNING: return "Running";
        case VM_STATE_PAUSED: return "Paused";
        case VM_STATE_ERROR: return "Error";
        case VM_STATE_FINISHED: return "Finished";
        default: return "Unknown";
    }
}

bool vm_has_error(const vm_context_t* ctx) {
    return ctx != NULL && ctx->state == VM_STATE_ERROR;
}

void vm_get_stats(const vm_context_t* ctx, uint32_t* instructions_executed, 
                  uint32_t* keys_pressed, uint32_t* keys_released) {
    if (ctx == NULL) {
        return;
    }
    
    if (instructions_executed) *instructions_executed = ctx->instructions_executed;
    if (keys_pressed) *keys_pressed = ctx->keys_pressed;
    if (keys_released) *keys_released = ctx->keys_released;
}
