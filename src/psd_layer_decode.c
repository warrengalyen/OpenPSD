/**
 * @file psd_layer_decode.c
 * @brief Layer channel data decoding implementation
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

#include "psd_layer_decode.h"
#include "psd_rle.h"
#include "psd_zip.h"
#include "psd_alloc.h"
#include <string.h>

#include <stdio.h>

/**
 * @brief Decode a single PackBits-encoded row
 *
 * Consumes exactly src_len bytes and produces exactly dst_len bytes.
 */
static psd_status_t psd_packbits_decode_row(
    const uint8_t *src, size_t src_len,
    uint8_t *dst, size_t dst_len)
{
    size_t si = 0;
    size_t di = 0;

    while (si < src_len && di < dst_len) {
        int8_t n = (int8_t)src[si++];
        if (n >= 0) {
            /* copy next (n+1) bytes literally */
            size_t count = (size_t)n + 1u;
            if (si + count > src_len) return PSD_ERR_CORRUPT_DATA;
            if (di + count > dst_len) return PSD_ERR_CORRUPT_DATA;
            memcpy(dst + di, src + si, count);
            si += count;
            di += count;
        } else if (n >= -127) {
            /* replicate next byte (1 - n) times */
            if (si >= src_len) return PSD_ERR_CORRUPT_DATA;
            uint8_t v = src[si++];
            size_t count = (size_t)(1 - n); /* n is negative */
            if (di + count > dst_len) return PSD_ERR_CORRUPT_DATA;
            memset(dst + di, v, count);
            di += count;
        }
        else {
            /* n == -128: no-op */
        }
    }

    /* Must consume exactly src_len and fill exactly dst_len */
    if (si != src_len || di != dst_len) {
        return PSD_ERR_CORRUPT_DATA;
    }
    return PSD_OK;
}

static uint16_t read_be_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static uint32_t read_be_u32(const uint8_t *p) {
   return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Try parsing row byte counts of size (height * row_bytes) */
static psd_status_t parse_rle_row_counts(
    const uint8_t *compressed, uint64_t compressed_len,
    uint32_t height, uint32_t row_bytes,
    uint64_t *out_counts_size, uint64_t *out_total_rle_bytes)
{
    uint64_t counts_size = (uint64_t)height * (uint64_t)row_bytes;
    if (compressed_len < counts_size) return PSD_ERR_CORRUPT_DATA;

    uint64_t total = 0;
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *p = compressed + (uint64_t)y * row_bytes;
        uint64_t v = (row_bytes == 2) ? (uint64_t)read_be_u16(p) : (uint64_t)read_be_u32(p);
        total += v;
        /* The total RLE bytes must fit in the buffer after the counts table */
        if (total > (compressed_len - counts_size)) return PSD_ERR_CORRUPT_DATA;
    }

    if (compressed_len < counts_size + total) return PSD_ERR_CORRUPT_DATA;

    *out_counts_size = counts_size;
    *out_total_rle_bytes = total;
    return PSD_OK;
}

 /**
 * @brief Decode a layer channel's pixel data
 *
 * Supports RAW and RLE decompression. ZIP formats are preserved as-is
 * since ZIP support is not yet implemented.
 */
psd_status_t psd_layer_channel_decode(
        psd_layer_channel_data_t *channel,
        uint32_t width,
        uint32_t height,
        uint16_t depth,
        const psd_allocator_t *allocator) {
    if (!channel) {
        return PSD_ERR_INVALID_ARGUMENT;
    }

    /* Already decoded? */
    if (channel->is_decoded && channel->decoded_data) {
        return PSD_OK;
    }

    /* Calculate expected decoded size */
    uint64_t bytes_per_sample = (depth >= 8) ? (uint64_t)(depth / 8) : 1;
    uint64_t scanline_width = 0;
    uint64_t expected_decoded_size = 0;

    if (depth == 1) {
        /* Bitmap channels are packed 1-bit per pixel per row */
        scanline_width = ((uint64_t)width + 7u) / 8u;
        expected_decoded_size = scanline_width * (uint64_t)height;
    } else {
        if (bytes_per_sample == 0) {
            return PSD_ERR_UNSUPPORTED_FEATURE;
        }
        scanline_width = (uint64_t)width * bytes_per_sample;
        expected_decoded_size = scanline_width * (uint64_t)height;
    }

    /* Handle different compression types */
    switch (channel->compression) {
        case 0: { /* RAW - uncompressed */
            if (channel->compressed_length < expected_decoded_size) {
                return PSD_ERR_CORRUPT_DATA;
            }

            /* RAW channel data may contain padding beyond the expected pixel payload.
             * Allocate an owned decoded buffer of exactly expected_decoded_size and
             * copy only the pixel bytes we expect. */
            uint8_t *decoded = (uint8_t *)psd_alloc_malloc(allocator, expected_decoded_size);
            if (!decoded) {
                return PSD_ERR_OUT_OF_MEMORY;
            }
            memcpy(decoded, channel->compressed_data, (size_t)expected_decoded_size);

            /* Data is already raw, just mark as decoded */
            channel->decoded_data = decoded;
            channel->decoded_length = expected_decoded_size;
            channel->is_decoded = true;

            return PSD_OK;
        }

        case 1: { /* RLE - PackBits compression */
            /* RLE layer channels store per-row byte counts followed by PackBits data.
             * PSD usually uses 2-byte counts; PSB commonly uses 4-byte counts.
             * We auto-detect based on plausibility AND prefer the interpretation that
             * exactly consumes the payload (counts + RLE bytes == compressed_len). */
            uint64_t counts_size = 0;
            uint64_t total_rle_bytes = 0;
            uint32_t row_count_bytes = 2;

            uint64_t counts_size2 = 0, total2 = 0;
            uint64_t counts_size4 = 0, total4 = 0;
            psd_status_t st2 = parse_rle_row_counts(
                channel->compressed_data, channel->compressed_length,
                height, 2, &counts_size2, &total2);
            psd_status_t st4 = parse_rle_row_counts(
                channel->compressed_data, channel->compressed_length,
                height, 4, &counts_size4, &total4);

            if (st2 == PSD_OK && st4 == PSD_OK) {
                uint64_t end2 = counts_size2 + total2;
                uint64_t end4 = counts_size4 + total4;
                if (end4 == channel->compressed_length && end2 != channel->compressed_length) {
                    row_count_bytes = 4; counts_size = counts_size4; total_rle_bytes = total4;
                } else if (end2 == channel->compressed_length && end4 != channel->compressed_length) {
                    row_count_bytes = 2; counts_size = counts_size2; total_rle_bytes = total2;
                } else {
                    /* Both (or neither) consume exactly; prefer 2-byte for compatibility. */
                    row_count_bytes = 2; counts_size = counts_size2; total_rle_bytes = total2;
                }
            } else if (st2 == PSD_OK) {
                row_count_bytes = 2; counts_size = counts_size2; total_rle_bytes = total2;
            } else if (st4 == PSD_OK) {
                row_count_bytes = 4; counts_size = counts_size4; total_rle_bytes = total4;
            } else {
                return PSD_ERR_CORRUPT_DATA;
            }

            /* Allocate buffer for decoded data */
            uint8_t *decoded = (uint8_t *)psd_alloc_malloc(allocator, expected_decoded_size);
            if (!decoded) {
                return PSD_ERR_OUT_OF_MEMORY;
            }

            /* Row-by-row PackBits decode using the byte counts table */
            const uint8_t *counts = channel->compressed_data;
            const uint8_t *rle = channel->compressed_data + counts_size;
            uint64_t rle_off = 0;

            for (uint32_t y = 0; y < height; y++) {
                uint64_t row_len = 0;
                const uint8_t *p = counts + (uint64_t)y * row_count_bytes;
                row_len = (row_count_bytes == 2) ? (uint64_t)read_be_u16(p) : (uint64_t)read_be_u32(p);
                if (rle_off + row_len > total_rle_bytes) {
                    psd_alloc_free(allocator, decoded);
                    return PSD_ERR_CORRUPT_DATA;
                }
                uint8_t *dst_row = decoded + (uint64_t)y * scanline_width;
                psd_status_t row_st = psd_packbits_decode_row(
                    rle + rle_off,
                    (size_t)row_len,
                    dst_row,
                    (size_t)scanline_width);
                if (row_st != PSD_OK) {
                    psd_alloc_free(allocator, decoded);
                    return row_st;
                }
                rle_off += row_len;
            }

            channel->decoded_data = decoded;
            channel->decoded_length = expected_decoded_size;
            channel->is_decoded = true;

            return PSD_OK;
        }

        case 2: { /* ZIP - zlib compression */
            printf("Attempting to decode ZIP channel data\n");
            /* Allocate buffer for decoded data */
            uint8_t *decoded = (uint8_t *)psd_alloc_malloc(allocator, expected_decoded_size);
            if (!decoded) {
                return PSD_ERR_OUT_OF_MEMORY;
            }

            /* Decompress ZIP data */
            psd_status_t status = psd_zip_decompress(
                channel->compressed_data,
                channel->compressed_length,
                decoded,
                expected_decoded_size,
                allocator);

            if (status != PSD_OK) {
                psd_alloc_free(allocator, decoded);
                /* If ZIP not supported, leave data compressed */
                if (status == PSD_ERR_UNSUPPORTED_COMPRESSION) {
                    channel->decoded_data = NULL;
                    channel->decoded_length = 0;
                    channel->is_decoded = false;
                    return PSD_OK;
                }
                return status;
            }

            channel->decoded_data = decoded;
            channel->decoded_length = expected_decoded_size;
            channel->is_decoded = true;

            printf("Successfully decoded ZIP channel data\n");
            return PSD_OK;
        }

        case 3: { /* ZIP with prediction */
            printf("Attempting to decode ZIP with prediction channel data\n");
            /* Allocate buffer for decoded data */
            uint8_t *decoded = (uint8_t *)psd_alloc_malloc(allocator, expected_decoded_size);
            if (!decoded) {
                return PSD_ERR_OUT_OF_MEMORY;
            }

            /* Decompress with prediction reversal */
            psd_status_t status = psd_zip_decompress_with_prediction(
                channel->compressed_data,
                channel->compressed_length,
                decoded,
                expected_decoded_size,
                (size_t)scanline_width,
                (size_t)((depth == 1) ? 1 : (depth / 8)),
                allocator);

            if (status != PSD_OK) {
                psd_alloc_free(allocator, decoded);
                /* If ZIP not supported, leave data compressed */
                if (status == PSD_ERR_UNSUPPORTED_COMPRESSION) {
                    channel->decoded_data = NULL;
                    channel->decoded_length = 0;
                    channel->is_decoded = false;
                    return PSD_OK;
                }
                return status;
            }

            channel->decoded_data = decoded;
            channel->decoded_length = expected_decoded_size;
            channel->is_decoded = true;

            printf("Successfully decoded ZIP with prediction channel data\n");
            return PSD_OK;
        }

        default:
            return PSD_ERR_UNSUPPORTED_COMPRESSION;
    }
}
