#ifndef LOG_BUFFER_H
#define LOG_BUFFER_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the log buffer system
 * @return true on success, false on failure
 */
bool log_buffer_init(void);

/**
 * @brief Get the number of bytes available to read from the log buffer
 * @return Number of bytes available
 */
uint32_t log_buffer_get_available(void);

/**
 * @brief Read a chunk of data from the log buffer
 * @param buffer Buffer to store the read data
 * @param max_size Maximum number of bytes to read
 * @return Number of bytes actually read (0 if no more data)
 */
uint32_t log_buffer_read_chunk(uint8_t *buffer, uint32_t max_size);

/**
 * @brief Start reading from the beginning of the log buffer
 * This resets the read pointer to the oldest available data
 */
void log_buffer_start_read(void);

/**
 * @brief Clear all data from the log buffer
 */
void log_buffer_clear(void);

/**
 * @brief Deinitialize the log buffer system and free memory
 */
void log_buffer_deinit(void);

/**
 * @brief Print to serial port only (bypasses log buffer)
 * Works like printf but only writes to serial, not the log buffer
 * @param fmt Format string (same as printf)
 * @param ... Variable arguments
 */
void log_serial_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif  // LOG_BUFFER_H
