#ifndef PROGRAM_STORAGE_H
#define PROGRAM_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROGRAM_STORAGE_PAGE_SIZE 4096                                        // Flash page size in bytes
#define PROGRAM_STORAGE_MAX_SIZE ((1024 * 1024) - PROGRAM_STORAGE_PAGE_SIZE)  // 1MB total minus reserved first page

/**
 * @brief Program storage write state
 */
typedef enum {
    PROGRAM_STORAGE_STATE_IDLE,
    PROGRAM_STORAGE_STATE_WRITING,
    PROGRAM_STORAGE_STATE_ERROR
} program_storage_write_state_t;

/**
 * @brief Program storage write source
 */
typedef enum {
    PROGRAM_STORAGE_SOURCE_NONE,
    PROGRAM_STORAGE_SOURCE_USB,
    PROGRAM_STORAGE_SOURCE_HTTP
} program_storage_source_t;

/**
 * @brief Initialize program storage
 * @return true on success, false on failure
 */
bool program_storage_init(void);

/**
 * @brief Get pointer to default program in flash
 * @param out_size Pointer to store program size
 * @return Pointer to program data in flash, or NULL if no program or error
 */
const uint8_t *program_storage_get(uint32_t *out_size);

/**
 * @brief Start writing a new program to flash (erases only necessary sectors)
 * @param expected_program_size The expected size of the program to be written
 * @param source The source requesting the write (USB or HTTP)
 * @note First 4KB page is reserved for size header, program data starts at page 1
 * @note Can interrupt an existing write session from a different source
 * @return true on success, false on failure
 */
bool program_storage_write_start(uint32_t expected_program_size, program_storage_source_t source);

/**
 * @brief Write a chunk of program data to flash (buffered internally)
 * @param data Pointer to data to write
 * @param size Size of data in bytes (must be <= page size)
 * @param source The source requesting the write (must match current owner)
 * @return true on success, false on failure
 */
bool program_storage_write_chunk(const uint8_t *data, uint32_t size, program_storage_source_t source);

/**
 * @brief Finish writing program to flash (commits the size header, which marks the program as "valid")
 * @param program_size The size, in bytes, of the program data
 * @param source The source requesting the finish (must match current owner)
 * @return true on success, false on failure
 */
bool program_storage_write_finish(uint32_t program_size, program_storage_source_t source);

/**
 * @brief Get current write state
 * @return Current write state
 */
program_storage_write_state_t program_storage_get_write_state(void);

/**
 * @brief Get number of bytes written so far
 * @return Number of bytes written
 */
uint32_t program_storage_get_bytes_written(void);

/**
 * @brief Get expected total size
 * @return Expected total size
 */
uint32_t program_storage_get_expected_size(void);

/**
 * @brief Erase program from flash
 * @return true on success, false on failure
 */
bool program_storage_erase(void);

#ifdef __cplusplus
}
#endif

#endif  // PROGRAM_STORAGE_H
