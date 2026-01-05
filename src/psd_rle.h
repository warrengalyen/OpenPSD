/**
 * @file psd_rle.h
 * @brief PSD RLE (Run-Length Encoding) decompression
 *
 * Implements PackBits-style RLE decompression as used in PSD files.
 * Each scanline is encoded independently.
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

#ifndef PSD_RLE_H
#define PSD_RLE_H

#include <stdint.h>
#include <stddef.h>
#include "../include/openpsd/psd_types.h"
#include "../include/openpsd/psd_error.h"
#include "../include/openpsd/psd_export.h"

/**
 * @brief Decompress a single RLE-encoded scanline (internal)
 *
 * PSD uses PackBits-style RLE where:
 * - 0-127: Next (n+1) bytes are literal
 * - 128-255: Next byte is repeated (257-n) times
 * - Byte value 128 means repeat next byte 129 times
 *
 * @param compressed Compressed scanline data
 * @param compressed_len Length of compressed data
 * @param width Expected scanline width in bytes
 * @param decompressed Output buffer (must be at least 'width' bytes)
 * @param out_len Output length (should equal width on success)
 * @return PSD_OK on success, PSD_ERR_CORRUPT_DATA if data is malformed
 */
PSD_INTERNAL psd_status_t psd_rle_decode_scanline(
    const uint8_t *compressed,
    size_t compressed_len,
    size_t width,
    uint8_t *decompressed,
    size_t *out_len);

/**
 * @brief Decompress a full RLE-compressed buffer (internal)
 *
 * Decompresses multiple scanlines (one after another in the input).
 *
 * @param compressed Full compressed buffer
 * @param compressed_len Length of compressed data
 * @param scanline_count Number of scanlines to decompress
 * @param width Width of each scanline in bytes
 * @param decompressed Output buffer (must be at least scanline_count * width bytes)
 * @param out_len Output length (should equal scanline_count * width on success)
 * @return PSD_OK on success, PSD_ERR_CORRUPT_DATA if any scanline is malformed
 */
PSD_INTERNAL psd_status_t psd_rle_decode(
    const uint8_t *compressed,
    size_t compressed_len,
    size_t scanline_count,
    size_t width,
    uint8_t *decompressed,
    size_t *out_len);

#endif /* PSD_RLE_H */
