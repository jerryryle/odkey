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
 * @brief Initialize program storage
 * @return true on success, false on failure
 */
bool program_storage_init(void);

/**
 * @brief Get pointer to default program in flash
 * @param out_size Pointer to store program size
 * @return Pointer to program data in flash, or NULL if no program or error
 */
const uint8_t *program_storage_get(size_t *out_size);

/**
 * @brief Start writing a new program to flash (erases only necessary sectors)
 * @param expected_program_size The expected size of the program to be written
 * @note First 4KB page is reserved for size header, program data starts at page 1
 * @return true on success, false on failure
 */
bool program_storage_write_start(size_t expected_program_size);

/**
 * @brief Write a full page of program data to flash
 * @param page_data Pointer to page data
 * @param page_size Size of page in bytes (must be exactly PROGRAM_STORAGE_PAGE_SIZE bytes)
 * @return true on success, false on failure
 */
bool program_storage_write_page(const uint8_t *page_data, size_t page_size);

/**
 * @brief Finish writing program to flash (commits the size header, which marks the program as "valid")
 * @param program_size The size, in bytes, of the program data
 * @return true on success, false on failure
 */
bool program_storage_write_finish(size_t program_size);

/**
 * @brief Erase program from flash
 * @return true on success, false on failure
 */
bool program_storage_erase(void);

#ifdef __cplusplus
}
#endif

#endif  // PROGRAM_STORAGE_H
