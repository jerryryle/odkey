#ifndef VM_TASK_H
#define VM_TASK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback function type for sending HID keyboard reports.
 * @param modifier Modifier key to send (or 0 to release all modifiers)
 * @param keys Array of keycodes to send (can be NULL to release all keys)
 * @param count Number of keycodes in the array (0-6, more than 6 will be truncated)
 * @return true on success, false on failure
 */
typedef bool (*vm_hid_send_callback_t)(uint8_t modifier,
                                       const uint8_t *keys,
                                       uint8_t count);

/**
 * @brief Initialize the VM task module
 * @param hid_send_callback Callback function for sending HID keyboard reports
 * @return true on success, false on failure
 */
bool vm_task_init(vm_hid_send_callback_t hid_send_callback);

/**
 * @brief Start a program execution in the VM task
 * @param program Pointer to program bytecode (must remain valid during execution)
 * @param program_size Size of program in bytes
 * @return true if request was queued successfully, false if already running or error
 */
bool vm_task_start_program(const uint8_t *program, uint32_t program_size);

/**
 * @brief Check if a program is currently running
 * @return true if program is running, false if idle
 */
bool vm_task_is_running(void);

/**
 * @brief Halt the currently running program
 * @note This function blocks until the program has stopped
 */
bool vm_task_halt(void);

#ifdef __cplusplus
}
#endif

#endif  // VM_TASK_H
