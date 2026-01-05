/**
 * @file psd_rle.c
 * @brief PSD RLE decompression implementation
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

#include "psd_rle.h"
#include <string.h>

/**
 * @brief Decompress a single RLE-encoded scanline
 *
 * PackBits RLE format:
 * - Byte 0-127: Number of literal bytes following is (n+1)
 * - Byte 128-255: Next single byte is repeated (257-n) times
 *   (Note: 128 = repeat 129 times, 255 = repeat 2 times)
 */
psd_status_t psd_rle_decode_scanline(
    const uint8_t *compressed,
    size_t compressed_len,
    size_t width,
    uint8_t *decompressed,
    size_t *out_len)
{
    if (!compressed || !decompressed || !out_len) {
        return PSD_ERR_INVALID_ARGUMENT;
    }

    size_t comp_pos = 0;
    size_t decomp_pos = 0;

    while (decomp_pos < width && comp_pos < compressed_len) {
        uint8_t header = compressed[comp_pos++];

        if (header < 128) {
            /* Literal run: next (header + 1) bytes are literal */
            size_t literal_count = header + 1;

            /* Verify we have enough space in output */
            if (decomp_pos + literal_count > width) {
                return PSD_ERR_CORRUPT_DATA;
            }

            /* Verify we have enough data in input */
            if (comp_pos + literal_count > compressed_len) {
                return PSD_ERR_CORRUPT_DATA;
            }

            /* Copy literal bytes */
            memcpy(decompressed + decomp_pos, compressed + comp_pos, literal_count);
            comp_pos += literal_count;
            decomp_pos += literal_count;
        } else if (header == 128) {
            /* No-op per PackBits spec (0x80) */
            continue;
        } else {
            /* Repeat run: repeat next byte (257 - header) times */
            size_t repeat_count = 257 - header;

            /* Verify we have the repeat byte */
            if (comp_pos >= compressed_len) {
                return PSD_ERR_CORRUPT_DATA;
            }

            /* Verify we have enough space in output */
            if (decomp_pos + repeat_count > width) {
                return PSD_ERR_CORRUPT_DATA;
            }

            uint8_t repeat_byte = compressed[comp_pos++];

            /* Fill with repeated byte */
            memset(decompressed + decomp_pos, repeat_byte, repeat_count);
            decomp_pos += repeat_count;
        }
    }

    /* Verify we decompressed exactly 'width' bytes */
    if (decomp_pos != width) {
        return PSD_ERR_CORRUPT_DATA;
    }

    *out_len = decomp_pos;
    return PSD_OK;
}

/**
 * @brief Decompress a full RLE-compressed buffer
 *
 * Decompresses multiple scanlines sequentially.
 */
psd_status_t psd_rle_decode(
    const uint8_t *compressed,
    size_t compressed_len,
    size_t scanline_count,
    size_t width,
    uint8_t *decompressed,
    size_t *out_len)
{
    if (!compressed || !decompressed || !out_len) {
        return PSD_ERR_INVALID_ARGUMENT;
    }

    if (scanline_count == 0 || width == 0) {
        *out_len = 0;
        return PSD_OK;
    }

    size_t total_decompressed = 0;
    size_t comp_pos = 0;

    for (size_t i = 0; i < scanline_count; i++) {
        /* Find how many bytes are consumed by scanning this scanline */
        const uint8_t *scan_ptr = compressed + comp_pos;
        size_t scan_pos = 0;
        size_t decomp_scanned = 0;

        /* First, figure out the length of this compressed scanline */
        while (decomp_scanned < width && (comp_pos + scan_pos) < compressed_len) {
            uint8_t header = scan_ptr[scan_pos++];

            if (header < 128) {
                size_t literal_count = header + 1;
                if (decomp_scanned + literal_count > width ||
                    (comp_pos + scan_pos + literal_count) > compressed_len) {
                    return PSD_ERR_CORRUPT_DATA;
                }
                scan_pos += literal_count;
                decomp_scanned += literal_count;
            } else if (header == 128) {
                /* No-op per PackBits spec (0x80) */
                continue;
            } else {
                size_t repeat_count = 257 - header;
                if ((comp_pos + scan_pos) >= compressed_len) {
                    return PSD_ERR_CORRUPT_DATA;
                }
                scan_pos++;
                decomp_scanned += repeat_count;
            }
        }

        if (decomp_scanned != width) {
            return PSD_ERR_CORRUPT_DATA;
        }

        /* Now decompress this scanline */
        size_t scanline_out_len = 0;
        psd_status_t err = psd_rle_decode_scanline(
            compressed + comp_pos,
            scan_pos,  /* Use the calculated length */
            width,
            decompressed + total_decompressed,
            &scanline_out_len);

        if (err != PSD_OK) {
            return err;
        }

        comp_pos += scan_pos;
        total_decompressed += scanline_out_len;

        /* Safety check: don't exceed buffer */
        if (comp_pos > compressed_len) {
            return PSD_ERR_CORRUPT_DATA;
        }
    }

    *out_len = total_decompressed;
    return PSD_OK;
}
