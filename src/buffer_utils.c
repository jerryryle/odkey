#include "buffer_utils.h"
#include <string.h>

bool bu_read_u8(const uint8_t *buf, size_t size, uint8_t *out) {
    if (buf == NULL || out == NULL || size < 1) {
        return false;
    }
    *out = buf[0];
    return true;
}

bool bu_read_u16_le(const uint8_t *buf, size_t size, uint16_t *out) {
    if (buf == NULL || out == NULL || size < 2) {
        return false;
    }
    *out = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    return true;
}

bool bu_read_u32_le(const uint8_t *buf, size_t size, uint32_t *out) {
    if (buf == NULL || out == NULL || size < 4) {
        return false;
    }
    *out = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) |
           ((uint32_t)buf[3] << 24);
    return true;
}

bool bu_read_bytes(const uint8_t *buf, size_t size, uint8_t *dst, size_t count) {
    if (buf == NULL || dst == NULL || size < count) {
        return false;
    }
    memcpy(dst, buf, count);
    return true;
}

bool bu_write_u8(uint8_t *dst, size_t size, uint8_t value) {
    if (dst == NULL || size < 1) {
        return false;
    }
    dst[0] = value;
    return true;
}

bool bu_write_u16_le(uint8_t *dst, size_t size, uint16_t value) {
    if (dst == NULL || size < 2) {
        return false;
    }
    dst[0] = value & 0xFF;
    dst[1] = (value >> 8) & 0xFF;
    return true;
}

bool bu_write_u32_le(uint8_t *dst, size_t size, uint32_t value) {
    if (dst == NULL || size < 4) {
        return false;
    }
    dst[0] = value & 0xFF;
    dst[1] = (value >> 8) & 0xFF;
    dst[2] = (value >> 16) & 0xFF;
    dst[3] = (value >> 24) & 0xFF;
    return true;
}

bool bu_write_bytes(uint8_t *dst, size_t size, const uint8_t *src, size_t count) {
    if (dst == NULL || src == NULL || size < count) {
        return false;
    }
    memcpy(dst, src, count);
    return true;
}
