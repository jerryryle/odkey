#ifndef PROGRAM_RAM_H
#define PROGRAM_RAM_H

#include <stdbool.h>
#include <stdint.h>
#include "program.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize RAM program
 * @return true on success, false on failure
 */
bool program_ram_init(void);

/**
 * @brief Get pointer to program in RAM
 * @param out_size Pointer to hold program size
 * @return Pointer to program data, or NULL if no program or error
 */
const uint8_t *program_ram_get(uint32_t *out_size);

/**
 * @brief Start writing a new program to RAM
 * @param expected_program_size The expected size of the program to be written
 * @param source The source requesting the write (USB or HTTP)
 * @return true on success, false on failure
 */
bool program_ram_write_start(uint32_t expected_program_size,
                             program_write_source_t source);

/**
 * @brief Write a chunk of program data to RAM
 * @param data Pointer to data to write
 * @param size Size of data in bytes
 * @param source The source requesting the write (must match current owner)
 * @return true on success, false on failure
 */
bool program_ram_write_chunk(const uint8_t *data,
                             uint32_t size,
                             program_write_source_t source);

/**
 * @brief Finish writing program to RAM
 * @param program_size The size, in bytes, of the program data
 * @param source The source requesting the finish (must match current owner)
 * @return true on success, false on failure
 */
bool program_ram_write_finish(uint32_t program_size, program_write_source_t source);

/**
 * @brief Get number of bytes written so far
 * @return Number of bytes written
 */
uint32_t program_ram_get_bytes_written(void);

/**
 * @brief Get expected total size
 * @return Expected total size
 */
uint32_t program_ram_get_expected_size(void);

/**
 * @brief Erase program from RAM
 * @return true on success, false on failure
 */
bool program_ram_erase(void);

#ifdef __cplusplus
}
#endif

#endif  // PROGRAM_RAM_H
