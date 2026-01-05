/**
 * @file psd_zip.c
 * @brief ZIP and ZIP-with-prediction decompression implementation
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

#include "psd_zip.h"
#include <string.h>
#include <stdlib.h>

#ifdef PSD_ENABLE_ZIP
#include <zlib.h>

/**
 * @brief Decompress ZIP-compressed data using zlib
 */
psd_status_t psd_zip_decompress(
    const uint8_t *compressed,
    size_t compressed_len,
    uint8_t *decompressed,
    size_t decompressed_len,
    const psd_allocator_t *allocator)
{
    (void)allocator;  /* zlib uses its own memory management */
    
    if (!compressed || !decompressed) {
        return PSD_ERR_INVALID_ARGUMENT;
    }
    
    /* PSD/PSB ZIP-compressed data is DEFLATE, but real-world files vary between
     * raw DEFLATE streams and zlib-wrapped streams. Try both. */
    for (int attempt = 0; attempt < 2; attempt++) {
        int wbits = (attempt == 0) ? -MAX_WBITS : MAX_WBITS;

        z_stream stream;
        memset(&stream, 0, sizeof(stream));

        int ret = inflateInit2(&stream, wbits);
        if (ret != Z_OK) {
            continue;
        }

        stream.avail_in = (uInt)compressed_len;
        stream.next_in = (uint8_t *)compressed;

        stream.avail_out = (uInt)decompressed_len;
        stream.next_out = decompressed;

        ret = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);

        if (ret == Z_STREAM_END && stream.total_out == decompressed_len) {
            return PSD_OK;
        }
    }

    return PSD_ERR_CORRUPT_DATA;
}

/**
 * @brief Paeth predictor function (from PNG specification)
 *
 * Used to reverse the PNG prediction filter.
 */
static inline uint8_t paeth_predictor(uint8_t a, uint8_t b, uint8_t c)
{
    int p = (int)a + (int)b - (int)c;
    int pa = abs(p - (int)a);
    int pb = abs(p - (int)b);
    int pc = abs(p - (int)c);
    
    if (pa <= pb && pa <= pc) {
        return a;
    } else if (pb <= pc) {
        return b;
    } else {
        return c;
    }
}

/**
 * @brief Reverse ZIP prediction filter (PNG prediction)
 *
 * In PSD files with ZIP prediction, each scanline is prefixed with a
 * filter byte that indicates which PNG filter was applied. We must
 * reverse this filter to get the original data.
 */
psd_status_t psd_zip_reverse_prediction(
    uint8_t *scanline_data,
    size_t scanline_length,
    size_t bytes_per_pixel)
{
    if (!scanline_data || scanline_length == 0) {
        return PSD_ERR_INVALID_ARGUMENT;
    }
    
    if (bytes_per_pixel == 0 || bytes_per_pixel > 8) {
        return PSD_ERR_INVALID_ARGUMENT;
    }
    
    /* First byte is the filter type */
    uint8_t filter_type = scanline_data[0];
    
    /* Remaining bytes are the filtered scanline data */
    uint8_t *data = scanline_data + 1;
    size_t data_len = scanline_length - 1;
    
    switch (filter_type) {
        case 0: {
            /* Filter type 0 (None) - no prediction applied */
            /* Move data back one byte to remove filter indicator */
            memmove(scanline_data, data, data_len);
            break;
        }
        
        case 1: {
            /* Filter type 1 (Sub) - each byte is delta from left neighbor */
            for (size_t i = bytes_per_pixel; i < data_len; i++) {
                data[i] = (uint8_t)((int)data[i] + (int)data[i - bytes_per_pixel]);
            }
            memmove(scanline_data, data, data_len);
            break;
        }
        
        case 2: {
            /* Filter type 2 (Up) - each byte is delta from above */
            /* Since we only have one scanline, above is 0 */
            memmove(scanline_data, data, data_len);
            break;
        }
        
        case 3: {
            /* Filter type 3 (Average) - each byte is delta from average of left and above */
            for (size_t i = 0; i < bytes_per_pixel; i++) {
                data[i] = (uint8_t)((int)data[i] + (int)data[i] / 2);
            }
            for (size_t i = bytes_per_pixel; i < data_len; i++) {
                int left = (int)data[i - bytes_per_pixel];
                int above = 0;  /* Above is 0 since we only have one scanline */
                int avg = (left + above) / 2;
                data[i] = (uint8_t)((int)data[i] + avg);
            }
            memmove(scanline_data, data, data_len);
            break;
        }
        
        case 4: {
            /* Filter type 4 (Paeth) - each byte is delta from Paeth predictor */
            for (size_t i = 0; i < bytes_per_pixel; i++) {
                data[i] = (uint8_t)((int)data[i] + paeth_predictor(0, 0, 0));
            }
            for (size_t i = bytes_per_pixel; i < data_len; i++) {
                uint8_t left = data[i - bytes_per_pixel];
                uint8_t above = 0;  /* Above is 0 */
                uint8_t diag = 0;   /* Diagonal (above-left) is 0 */
                uint8_t pred = paeth_predictor(left, above, diag);
                data[i] = (uint8_t)((int)data[i] + (int)pred);
            }
            memmove(scanline_data, data, data_len);
            break;
        }
        
        default: {
            /* Unknown filter type */
            return PSD_ERR_CORRUPT_DATA;
        }
    }
    
    return PSD_OK;
}

/**
 * @brief Decompress ZIP data with prediction
 */
psd_status_t psd_zip_decompress_with_prediction(
    const uint8_t *compressed,
    size_t compressed_len,
    uint8_t *decompressed,
    size_t decompressed_len,
    size_t scanline_width,
    size_t bytes_per_pixel,
    const psd_allocator_t *allocator)
{
    if (scanline_width == 0) {
        return PSD_ERR_INVALID_ARGUMENT;
    }
    
    /* Decompress first */
    psd_status_t status = psd_zip_decompress(
        compressed, compressed_len,
        decompressed, decompressed_len,
        allocator);
    
    if (status != PSD_OK) {
        return status;
    }
    
    /* Each scanline is prefixed with a filter byte, so scanline_length includes it */
    size_t scanline_length = scanline_width + 1;
    
    /* Reverse prediction for each scanline */
    for (size_t offset = 0; offset < decompressed_len; offset += scanline_width) {
        if (offset + scanline_length > decompressed_len) {
            break;  /* Incomplete scanline, skip */
        }
        
        status = psd_zip_reverse_prediction(
            &decompressed[offset],
            scanline_length,
            bytes_per_pixel);
        
        if (status != PSD_OK) {
            return status;
        }
    }
    
    return PSD_OK;
}

#else  /* PSD_ENABLE_ZIP not defined */

/**
 * @brief Stub function when zlib is not available
 */
psd_status_t psd_zip_decompress(
    const uint8_t *compressed,
    size_t compressed_len,
    uint8_t *decompressed,
    size_t decompressed_len,
    const psd_allocator_t *allocator)
{
    (void)compressed;
    (void)compressed_len;
    (void)decompressed;
    (void)decompressed_len;
    (void)allocator;
    
    return PSD_ERR_UNSUPPORTED_COMPRESSION;
}

/**
 * @brief Stub function when zlib is not available
 */
psd_status_t psd_zip_reverse_prediction(
    uint8_t *scanline_data,
    size_t scanline_length,
    size_t bytes_per_pixel)
{
    (void)scanline_data;
    (void)scanline_length;
    (void)bytes_per_pixel;
    
    return PSD_ERR_UNSUPPORTED_COMPRESSION;
}

/**
 * @brief Stub function when zlib is not available
 */
psd_status_t psd_zip_decompress_with_prediction(
    const uint8_t *compressed,
    size_t compressed_len,
    uint8_t *decompressed,
    size_t decompressed_len,
    size_t scanline_width,
    size_t bytes_per_pixel,
    const psd_allocator_t *allocator)
{
    (void)compressed;
    (void)compressed_len;
    (void)decompressed;
    (void)decompressed_len;
    (void)scanline_width;
    (void)bytes_per_pixel;
    (void)allocator;
    
    return PSD_ERR_UNSUPPORTED_COMPRESSION;
}

#endif  /* PSD_ENABLE_ZIP */
