#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file buffer_utils.h
 * @brief Buffer manipulation utilities
 *
 * Provides bounds-checked read/write functions for little-endian byte operations.
 * All functions validate buffer bounds before performing operations.
 */

/**
 * @brief Read 8-bit unsigned integer from buffer
 * @param buf Source buffer
 * @param size Available size in buffer
 * @param out Output parameter for the value
 * @return true if successful, false if insufficient data
 */
bool bu_read_u8(const uint8_t *buf, size_t size, uint8_t *out);

/**
 * @brief Read 16-bit unsigned integer from buffer (little-endian)
 * @param buf Source buffer
 * @param size Available size in buffer
 * @param out Output parameter for the value
 * @return true if successful, false if insufficient data
 */
bool bu_read_u16_le(const uint8_t *buf, size_t size, uint16_t *out);

/**
 * @brief Read 32-bit unsigned integer from buffer (little-endian)
 * @param buf Source buffer
 * @param size Available size in buffer
 * @param out Output parameter for the value
 * @return true if successful, false if insufficient data
 */
bool bu_read_u32_le(const uint8_t *buf, size_t size, uint32_t *out);

/**
 * @brief Read array of bytes from buffer
 * @param buf Source buffer
 * @param size Available size in buffer
 * @param dst Destination buffer
 * @param count Number of bytes to read
 * @return true if successful, false if insufficient data
 */
bool bu_read_bytes(const uint8_t *buf, size_t size, uint8_t *dst, size_t count);

/**
 * @brief Write 8-bit unsigned integer to buffer
 * @param dst Destination buffer
 * @param size Available size in buffer
 * @param value Value to write
 * @return true if successful, false if insufficient space
 */
bool bu_write_u8(uint8_t *dst, size_t size, uint8_t value);

/**
 * @brief Write 16-bit unsigned integer to buffer (little-endian)
 * @param dst Destination buffer
 * @param size Available size in buffer
 * @param value Value to write
 * @return true if successful, false if insufficient space
 */
bool bu_write_u16_le(uint8_t *dst, size_t size, uint16_t value);

/**
 * @brief Write 32-bit unsigned integer to buffer (little-endian)
 * @param dst Destination buffer
 * @param size Available size in buffer
 * @param value Value to write
 * @return true if successful, false if insufficient space
 */
bool bu_write_u32_le(uint8_t *dst, size_t size, uint32_t value);

/**
 * @brief Write array of bytes to buffer
 * @param dst Destination buffer
 * @param size Available size in buffer
 * @param src Source buffer
 * @param count Number of bytes to write
 * @return true if successful, false if insufficient space
 */
bool bu_write_bytes(uint8_t *dst, size_t size, const uint8_t *src, size_t count);
