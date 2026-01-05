/**
 * @file psd_zip.h
 * @brief ZIP and ZIP-with-prediction decompression
 *
 * Implements decompression of ZIP-compressed and ZIP-with-prediction
 * compressed data as used in PSD composite and layer data.
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

#ifndef PSD_ZIP_H
#define PSD_ZIP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../include/openpsd/psd_types.h"
#include "../include/openpsd/psd_error.h"
#include "../include/openpsd/psd_export.h"

/**
 * @brief Prediction filter type (used in ZIP with prediction)
 */
typedef enum {
    PSD_ZIP_PREDICTION_NONE = 0,
    PSD_ZIP_PREDICTION_PAETH = 1,
} psd_zip_prediction_t;

/**
 * @brief Decompress ZIP-compressed data (internal)
 *
 * Decompresses zlib/ZIP-compressed data. Requires zlib library support.
 *
 * @param compressed Compressed data buffer
 * @param compressed_len Length of compressed data
 * @param decompressed Output buffer for decompressed data
 * @param decompressed_len Expected length of decompressed data
 * @param allocator Memory allocator
 * @return PSD_OK on success, error code on failure
 *         PSD_ERR_UNSUPPORTED_COMPRESSION if zlib not available at build time
 */
PSD_INTERNAL psd_status_t psd_zip_decompress(
    const uint8_t *compressed,
    size_t compressed_len,
    uint8_t *decompressed,
    size_t decompressed_len,
    const psd_allocator_t *allocator);

/**
 * @brief Reverse ZIP prediction filter applied to scanline (internal)
 *
 * Reverses the prediction filter applied during PNG prediction.
 * This must be done scanline-by-scanline after decompression.
 *
 * @param scanline_data Compressed scanline data (includes prediction byte)
 * @param scanline_length Length of scanline including prediction byte
 * @param bytes_per_pixel Number of bytes per pixel (depth / 8)
 * @return PSD_OK on success, error code on failure
 */
PSD_INTERNAL psd_status_t psd_zip_reverse_prediction(
    uint8_t *scanline_data,
    size_t scanline_length,
    size_t bytes_per_pixel);

/**
 * @brief Decompress ZIP data with prediction (internal)
 *
 * Decompresses ZIP-compressed data that was compressed with prediction.
 * Automatically applies prediction reversal to output.
 *
 * @param compressed Compressed data buffer
 * @param compressed_len Length of compressed data
 * @param decompressed Output buffer for decompressed data
 * @param decompressed_len Expected length of decompressed data
 * @param scanline_width Width of each scanline in bytes
 * @param bytes_per_pixel Number of bytes per pixel (depth / 8)
 * @param allocator Memory allocator
 * @return PSD_OK on success, error code on failure
 */
PSD_INTERNAL psd_status_t psd_zip_decompress_with_prediction(
    const uint8_t *compressed,
    size_t compressed_len,
    uint8_t *decompressed,
    size_t decompressed_len,
    size_t scanline_width,
    size_t bytes_per_pixel,
    const psd_allocator_t *allocator);

#endif /* PSD_ZIP_H */
