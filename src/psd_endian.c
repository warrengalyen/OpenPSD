/**
 * @file psd_endian.c
 * @brief Byte order conversion utilities
 *
 * Provides functions for converting between host byte order and big-endian.
 * PSD files always use big-endian byte order.
 *
 * Part of the OpenPSD library.
 * 
 * Copyright (c) 2025-2026 Warren Galyen
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/**
 * @brief Check if this system is little-endian
 *
 * @return true if little-endian, false if big-endian
 */
static inline bool psd_is_little_endian(void)
{
    static const uint32_t test = 0x01020304;
    const uint8_t *bytes = (const uint8_t *)&test;
    return bytes[0] == 0x04;
}

/**
 * @brief Swap bytes in 16-bit value
 *
 * @param value Value to swap
 * @return Byte-swapped value
 */
static inline uint16_t psd_swap16(uint16_t value)
{
    return ((value & 0xFF) << 8) | ((value >> 8) & 0xFF);
}

/**
 * @brief Swap bytes in 32-bit value
 *
 * @param value Value to swap
 * @return Byte-swapped value
 */
static inline uint32_t psd_swap32(uint32_t value)
{
    return ((value & 0xFF) << 24)
         | ((value & 0xFF00) << 8)
         | ((value & 0xFF0000) >> 8)
         | ((value >> 24) & 0xFF);
}

/**
 * @brief Swap bytes in 64-bit value
 *
 * @param value Value to swap
 * @return Byte-swapped value
 */
static inline uint64_t psd_swap64(uint64_t value)
{
    return ((value & 0x00000000000000FFULL) << 56)
         | ((value & 0x000000000000FF00ULL) << 40)
         | ((value & 0x0000000000FF0000ULL) << 24)
         | ((value & 0x00000000FF000000ULL) << 8)
         | ((value & 0x000000FF00000000ULL) >> 8)
         | ((value & 0x0000FF0000000000ULL) >> 24)
         | ((value & 0x00FF000000000000ULL) >> 40)
         | ((value & 0xFF00000000000000ULL) >> 56);
}

/**
 * @brief Read big-endian 16-bit integer from buffer
 *
 * @param buffer Buffer containing at least 2 bytes
 * @return Value in host byte order
 */
uint16_t psd_read_be16(const uint8_t *buffer)
{
    uint16_t value;
    memcpy(&value, buffer, sizeof(uint16_t));
    if (psd_is_little_endian()) {
        value = psd_swap16(value);
    }
    return value;
}

/**
 * @brief Read big-endian 32-bit integer from buffer
 *
 * @param buffer Buffer containing at least 4 bytes
 * @return Value in host byte order
 */
uint32_t psd_read_be32(const uint8_t *buffer)
{
    uint32_t value;
    memcpy(&value, buffer, sizeof(uint32_t));
    if (psd_is_little_endian()) {
        value = psd_swap32(value);
    }
    return value;
}

/**
 * @brief Read big-endian 64-bit integer from buffer
 *
 * @param buffer Buffer containing at least 8 bytes
 * @return Value in host byte order
 */
uint64_t psd_read_be64(const uint8_t *buffer)
{
    uint64_t value;
    memcpy(&value, buffer, sizeof(uint64_t));
    if (psd_is_little_endian()) {
        value = psd_swap64(value);
    }
    return value;
}

/**
 * @brief Write big-endian 16-bit integer to buffer
 *
 * @param buffer Buffer to write to (must have space for 2 bytes)
 * @param value Value to write (in host byte order)
 */
void psd_write_be16(uint8_t *buffer, uint16_t value)
{
    if (psd_is_little_endian()) {
        value = psd_swap16(value);
    }
    memcpy(buffer, &value, sizeof(uint16_t));
}

/**
 * @brief Write big-endian 32-bit integer to buffer
 *
 * @param buffer Buffer to write to (must have space for 4 bytes)
 * @param value Value to write (in host byte order)
 */
void psd_write_be32(uint8_t *buffer, uint32_t value)
{
    if (psd_is_little_endian()) {
        value = psd_swap32(value);
    }
    memcpy(buffer, &value, sizeof(uint32_t));
}

/**
 * @brief Write big-endian 64-bit integer to buffer
 *
 * @param buffer Buffer to write to (must have space for 8 bytes)
 * @param value Value to write (in host byte order)
 */
void psd_write_be64(uint8_t *buffer, uint64_t value)
{
    if (psd_is_little_endian()) {
        value = psd_swap64(value);
    }
    memcpy(buffer, &value, sizeof(uint64_t));
}

/**
 * @brief Read big-endian signed 32-bit integer from buffer
 *
 * @param buffer Buffer containing at least 4 bytes
 * @return Value in host byte order
 */
int32_t psd_read_be_i32(const uint8_t *buffer)
{
    /* Read as unsigned first, then reinterpret as signed */
    uint32_t unsigned_value = psd_read_be32(buffer);
    return (int32_t)unsigned_value;
}

/**
 * @brief Convert uint64_t to size_t with overflow detection
 *
 * Safely converts a 64-bit unsigned value to size_t, detecting overflow.
 * On 64-bit systems, size_t is 64-bit so most values fit.
 * On 32-bit systems, size_t is 32-bit so values > 2^32-1 overflow.
 *
 * @param value64 The 64-bit value to convert
 * @param out_size Where to store the converted size_t value
 * @return 0 on success, -1 if overflow would occur
 */
int psd_u64_to_size(uint64_t value64, size_t *out_size)
{
    if (!out_size) {
        return -1;
    }

    /* Check if value64 fits in size_t */
    if (sizeof(size_t) < sizeof(uint64_t)) {
        /* On 32-bit systems, size_t is smaller than uint64_t */
        size_t max_size = (size_t)-1; /* SIZE_MAX */
        if (value64 > (uint64_t)max_size) {
            return -1; /* Overflow */
        }
    }

    *out_size = (size_t)value64;
    return 0;
}
