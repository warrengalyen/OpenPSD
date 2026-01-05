/**
 * @file psd_header.h
 * @brief Internal PSD/PSB file header structures
 *
 * Defines the in-memory representation of PSD/PSB file headers.
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

#ifndef PSD_HEADER_H
#define PSD_HEADER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief PSD/PSB file header
 *
 * This structure represents the parsed PSD file header.
 * The file format is:
 *   - Signature (4 bytes): "8BPS" (0x38425053 in big-endian)
 *   - Version (2 bytes): 1 for PSD, 2 for PSB (large documents)
 *   - Reserved (6 bytes): must be 0
 *   - Channels (2 bytes): number of color channels (1-56)
 *   - Height (4 bytes): image height in pixels (1-30000 for PSD, 1-300000 for PSB)
 *   - Width (4 bytes): image width in pixels (1-30000 for PSD, 1-300000 for PSB)
 *   - Depth (2 bytes): bits per channel (1, 8, 16, or 32)
 *   - Color Mode (2 bytes): color mode enumeration
 */
typedef struct {
    uint32_t signature;        /**< "8BPS" (0x38425053 big-endian) */
    uint16_t version;          /**< 1=PSD, 2=PSB */
    uint16_t channels;         /**< Number of color channels */
    uint32_t height;           /**< Image height in pixels */
    uint32_t width;            /**< Image width in pixels */
    uint16_t depth;            /**< Bits per channel (1, 8, 16, or 32) */
    uint16_t color_mode;       /**< Color mode (0-13, possibly more in future) */
} psd_header_t;

/**
 * @brief PSD signature constant
 *
 * The magic number at the start of every PSD/PSB file.
 * "8BPS" in ASCII: 0x3842505350 big-endian
 */
#define PSD_SIGNATURE 0x38425053

/**
 * @brief PSD version (standard PSD format)
 *
 * Used for files up to 30000 x 30000 pixels
 */
#define PSD_VERSION_PSD 1

/**
 * @brief PSB version (large document format)
 *
 * Used for files up to 300000 x 300000 pixels
 */
#define PSD_VERSION_PSB 2

/**
 * @brief Maximum channels in PSD/PSB
 */
#define PSD_MAX_CHANNELS 56

/**
 * @brief Maximum dimension for standard PSD
 */
#define PSD_MAX_DIMENSION_STANDARD 30000

/**
 * @brief Maximum dimension for PSB (large documents)
 */
#define PSD_MAX_DIMENSION_PSB 300000

/**
 * @brief Color Mode Data section
 *
 * The Color Mode Data section contains color table data for indexed color images,
 * duotone information, or is empty for other color modes.
 *
 * For indexed color mode: Contains a 256 * 3 byte palette (RGB triplets)
 * For other modes: Usually empty but may contain additional data
 */
typedef struct {
    uint8_t *data;          /**< Raw color mode data (may be NULL if length is 0) */
    uint64_t length;        /**< Length of color mode data in bytes */
} psd_color_mode_data_t;

#endif /* PSD_HEADER_H */
