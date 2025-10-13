#ifndef PROGRAM_STORAGE_H
#define PROGRAM_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROGRAM_STORAGE_MAX_SIZE (1024 * 1024)  // 1MB total

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
const uint8_t* program_storage_get(size_t* out_size);

/**
 * @brief Start writing a new program to flash (erases partition first)
 * @return true on success, false on failure
 */
bool program_storage_write_start(void);

/**
 * @brief Write a chunk of program data to flash
 * @param chunk Pointer to chunk data
 * @param chunk_size Size of chunk in bytes (must be multiple of 4 for ESP32 flash alignment)
 * @return true on success, false on failure
 */
bool program_storage_write_chunk(const uint8_t* chunk, size_t chunk_size);

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

#endif // PROGRAM_STORAGE_H
