#ifndef PROGRAM_H
#define PROGRAM_H

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
typedef bool (*program_hid_send_callback_t)(uint8_t modifier,
                                            const uint8_t *keys,
                                            uint8_t count);

/**
 * @brief Callback function type for program execution completion
 * @param arg Optional argument passed to the callback
 */
typedef void (*program_execution_complete_callback_t)(void *arg);

#define PROGRAM_FLASH_PAGE_SIZE 4096  // Flash page size in bytes
#define PROGRAM_FLASH_MAX_SIZE \
    ((1024 * 1024) - PROGRAM_FLASH_PAGE_SIZE)  // Flash program max size in bytes
#define PROGRAM_RAM_MAX_SIZE \
    (1024 * 1024)  // RAM program max size in bytes (1MB in PSRAM)

/**
 * @brief Program type
 */
typedef enum { PROGRAM_TYPE_FLASH, PROGRAM_TYPE_RAM } program_type_t;

/**
 * @brief Program write source
 */
typedef enum {
    PROGRAM_WRITE_SOURCE_NONE,
    PROGRAM_WRITE_SOURCE_USB,
    PROGRAM_WRITE_SOURCE_HTTP
} program_write_source_t;

/**
 * @brief Initialize program storage and VM task
 * @param hid_send_callback Callback for VM to send HID keyboard reports
 * @return true on success, false on failure
 */
bool program_init(program_hid_send_callback_t hid_send_callback);

/**
 * @brief Get pointer to program
 * @param type Program type (FLASH or RAM)
 * @param out_size Pointer to store program size
 * @return Pointer to program data, or NULL if no program or error
 */
const uint8_t *program_get(program_type_t type, uint32_t *out_size);

/**
 * @brief Start writing a new program
 * @param type Program type (FLASH or RAM)
 * @param expected_program_size The expected size of the program to be written
 * @param source The source requesting the write (USB or HTTP)
 * @note For FLASH: First 4KB page is reserved for size header, program data starts at
 * page 1
 * @note Can interrupt an existing write session from a different source
 * @return true on success, false on failure
 */
bool program_write_start(program_type_t type,
                         uint32_t expected_program_size,
                         program_write_source_t source);

/**
 * @brief Write a chunk of program data (buffered internally)
 * @param type Program type (FLASH or RAM)
 * @param data Pointer to data to write
 * @param size Size of data in bytes (must be <= page size for FLASH)
 * @param source The source requesting the write (must match current owner)
 * @return true on success, false on failure
 */
bool program_write_chunk(program_type_t type,
                         const uint8_t *data,
                         uint32_t size,
                         program_write_source_t source);

/**
 * @brief Finish writing program (commits the size header for FLASH)
 * @param type Program type (FLASH or RAM)
 * @param program_size The size, in bytes, of the program data
 * @param source The source requesting the finish (must match current owner)
 * @return true on success, false on failure
 */
bool program_write_finish(program_type_t type,
                          uint32_t program_size,
                          program_write_source_t source);

/**
 * @brief Get number of bytes written so far
 * @param type Program type (FLASH or RAM)
 * @return Number of bytes written
 */
uint32_t program_get_bytes_written(program_type_t type);

/**
 * @brief Get expected total size
 * @param type Program type (FLASH or RAM)
 * @return Expected total size
 */
uint32_t program_get_expected_size(program_type_t type);

/**
 * @brief Erase program
 * @param type Program type (FLASH or RAM)
 * @return true on success, false on failure
 */
bool program_erase(program_type_t type);

/**
 * @brief Execute a program from storage
 * @param type Program type (FLASH or RAM)
 * @param on_complete Optional callback invoked when program execution completes
 * @param on_complete_arg Optional argument passed to the completion callback
 * @return true if program started, false if no program, already running, or error
 */
bool program_execute(program_type_t type,
                     program_execution_complete_callback_t on_complete,
                     void *on_complete_arg);

/**
 * @brief Check if a program is currently running
 * @return true if running, false if idle
 */
bool program_is_running(void);

/**
 * @brief Halt the currently running program
 * @return true if halted successfully, false if not running or error
 */
bool program_halt(void);

#ifdef __cplusplus
}
#endif

#endif  // PROGRAM_H
