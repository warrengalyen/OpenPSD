/**
 * @file psd_endian.h
 * @brief Internal endian utilities
 *
 * Internal header with endian conversion functions.
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
 * Part of the OpenPSD library.
 */

#ifndef PSD_ENDIAN_H
#define PSD_ENDIAN_H

#include <stdint.h>
#include "../include/openpsd/psd_export.h"

/**
 * @brief Read big-endian 16-bit integer from buffer (internal)
 *
 * @param buffer Buffer containing at least 2 bytes
 * @return Value in host byte order
 */
PSD_INTERNAL uint16_t psd_read_be16(const uint8_t *buffer);

/**
 * @brief Read big-endian 32-bit integer from buffer (internal)
 *
 * @param buffer Buffer containing at least 4 bytes
 * @return Value in host byte order
 */
PSD_INTERNAL uint32_t psd_read_be32(const uint8_t *buffer);

/**
 * @brief Read big-endian 64-bit integer from buffer (internal)
 *
 * @param buffer Buffer containing at least 8 bytes
 * @return Value in host byte order
 */
PSD_INTERNAL uint64_t psd_read_be64(const uint8_t *buffer);

/**
 * @brief Write big-endian 16-bit integer to buffer (internal)
 *
 * @param buffer Buffer to write to (must have space for 2 bytes)
 * @param value Value to write (in host byte order)
 */
PSD_INTERNAL void psd_write_be16(uint8_t *buffer, uint16_t value);

/**
 * @brief Write big-endian 32-bit integer to buffer (internal)
 *
 * @param buffer Buffer to write to (must have space for 4 bytes)
 * @param value Value to write (in host byte order)
 */
PSD_INTERNAL void psd_write_be32(uint8_t *buffer, uint32_t value);

/**
 * @brief Write big-endian 64-bit integer to buffer (internal)
 *
 * @param buffer Buffer to write to (must have space for 8 bytes)
 * @param value Value to write (in host byte order)
 */
PSD_INTERNAL void psd_write_be64(uint8_t *buffer, uint64_t value);

/**
 * @brief Read big-endian signed 32-bit integer from buffer (internal)
 *
 * @param buffer Buffer containing at least 4 bytes
 * @return Value in host byte order
 */
PSD_INTERNAL int32_t psd_read_be_i32(const uint8_t *buffer);

/**
 * @brief Convert uint64_t to size_t with overflow detection (internal)
 *
 * Safely converts a 64-bit unsigned value to size_t, detecting overflow.
 * This is critical for PSD length fields which must NEVER overflow size_t.
 *
 * @param value64 The 64-bit value to convert
 * @param out_size Where to store the converted size_t value
 * @return 0 on success, -1 if overflow would occur
 */
PSD_INTERNAL int psd_u64_to_size(uint64_t value64, size_t *out_size);

#endif /* PSD_ENDIAN_H */
