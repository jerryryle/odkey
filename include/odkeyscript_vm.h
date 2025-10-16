#ifndef ODKEYSCRIPT_VM_H
#define ODKEYSCRIPT_VM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// VM Error codes
typedef enum {
    VM_ERROR_NONE = 0,
    VM_ERROR_INVALID_OPCODE,
    VM_ERROR_INVALID_OPERAND,
    VM_ERROR_INVALID_ADDRESS,
    VM_ERROR_HID_ERROR,
    VM_ERROR_INVALID_PROGRAM
} vm_error_t;

// VM State
typedef enum {
    VM_STATE_READY,
    VM_STATE_RUNNING,
    VM_STATE_PAUSED,
    VM_STATE_ERROR,
    VM_STATE_FINISHED
} vm_state_t;

// Callback function types
typedef bool (*vm_hid_callback_t)(uint8_t modifier, const uint8_t *keys, uint8_t count);
typedef void (*vm_delay_callback_t)(uint16_t ms);

// VM Configuration
#define VM_MAX_COUNTERS 256
#define VM_MAX_KEYS_PRESSED 6

// VM Context structure
typedef struct {
    // Program memory
    const uint8_t *program;
    size_t program_size;
    size_t pc;  // Program counter

    // Counters for repeat loops
    uint16_t counters[VM_MAX_COUNTERS];

    // Current key state
    uint8_t current_modifier;
    uint8_t current_keys[VM_MAX_KEYS_PRESSED];
    uint8_t current_key_count;

    // Current press time setting
    uint16_t current_press_time;

    // VM state
    vm_state_t state;
    vm_error_t error;
    bool zero_flag;  // Set when a counter reaches zero, cleared by other operations

    // Callbacks
    vm_hid_callback_t hid_callback;
    vm_delay_callback_t delay_callback;

    // Statistics
    uint32_t instructions_executed;
    uint32_t keys_pressed;
    uint32_t keys_released;
} vm_context_t;

/**
 * @brief Initialize the VM context
 * @param ctx VM context to initialize
 * @return true on success, false on failure
 */
bool vm_init(vm_context_t *ctx);

/**
 * @brief Start execution of an ODKeyScript program
 * @param ctx VM context (must be initialized)
 * @param program Pointer to program bytecode
 * @param program_size Size of program in bytes
 * @param hid_callback Function to call for HID reports
 * @param delay_callback Function to call for delays
 * @return VM error code
 */
vm_error_t vm_start(vm_context_t *ctx, const uint8_t *program, size_t program_size, vm_hid_callback_t hid_callback, vm_delay_callback_t delay_callback);

/**
 * @brief Execute the next opcode in the program
 * @param ctx VM context (must be started)
 * @return VM error code
 */
vm_error_t vm_step(vm_context_t *ctx);

/**
 * @brief Check if the VM is currently running
 * @param ctx VM context
 * @return true if running, false otherwise
 */
bool vm_running(const vm_context_t *ctx);

/**
 * @brief Get human-readable error message
 * @param error Error code
 * @return Error message string
 */
const char *vm_error_to_string(vm_error_t error);

/**
 * @brief Get VM state as string
 * @param state VM state
 * @return State string
 */
const char *vm_state_to_string(vm_state_t state);

/**
 * @brief Check if VM is in an error state
 * @param ctx VM context
 * @return true if in error state, false otherwise
 */
bool vm_has_error(const vm_context_t *ctx);

/**
 * @brief Get VM statistics
 * @param ctx VM context
 * @param instructions_executed Output: number of instructions executed
 * @param keys_pressed Output: number of keys pressed
 * @param keys_released Output: number of keys released
 */
void vm_get_stats(const vm_context_t *ctx, uint32_t *instructions_executed, uint32_t *keys_pressed, uint32_t *keys_released);

/**
 * @brief Reset VM context to initial state
 * @param ctx VM context to reset
 */
void vm_reset(vm_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif  // ODKEYSCRIPT_VM_H
