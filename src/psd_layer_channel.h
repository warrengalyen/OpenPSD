/**
 * @file psd_layer_channel.h
 * @brief Layer channel data handling
 *
 * Manages layer channel pixel data with support for lazy decoding
 * and multiple compression formats.
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

#ifndef PSD_LAYER_CHANNEL_H
#define PSD_LAYER_CHANNEL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Layer channel data with lazy decoding support
 *
 * Stores channel pixel data which can be in raw, RLE, or ZIP format.
 * Decoding is deferred until explicitly requested.
 */
typedef struct {
    int16_t channel_id;           /**< Channel ID (-1=transparency, 0=R, 1=G, etc.) */
    uint8_t compression;          /**< Compression type: 0=RAW, 1=RLE, 2=ZIP, 3=ZIP+pred */
    uint64_t compressed_length;   /**< Length of compressed data */
    uint8_t *compressed_data;     /**< Compressed/raw pixel data (owned by allocator) */
    
    /* Decoded data (lazy) */
    uint8_t *decoded_data;        /**< Decoded pixel data (NULL until decoded, owned by allocator) */
    uint64_t decoded_length;      /**< Length of decoded data */
    bool is_decoded;              /**< Whether data has been decoded */
} psd_layer_channel_data_t;

#endif /* PSD_LAYER_CHANNEL_H */
