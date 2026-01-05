/**
 * @file psd_layer_decode.h
 * @brief Layer channel data decoding
 *
 * Provides lazy decoding of layer channel pixel data from compressed formats.
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

#ifndef PSD_LAYER_DECODE_H
#define PSD_LAYER_DECODE_H

#include "psd_layer_channel.h"
#include "../include/openpsd/psd_types.h"
#include "../include/openpsd/psd_error.h"
#include "../include/openpsd/psd_export.h"

/**
 * @brief Decode a layer channel's pixel data
 *
 * Decompresses the channel data if needed. Handles RAW and RLE formats.
 * ZIP-compressed data is left as-is if ZIP support is not enabled.
 *
 * @param channel Channel to decode (will be modified in-place)
 * @param width Layer width in pixels
 * @param height Layer height in pixels
 * @param depth Bit depth (8, 16, or 32)
 * @param allocator Memory allocator
 * @return PSD_OK on success, error code on failure
 */
PSD_INTERNAL psd_status_t psd_layer_channel_decode(
    psd_layer_channel_data_t *channel,
    uint32_t width,
    uint32_t height,
    uint16_t depth,
    const psd_allocator_t *allocator
);

#endif /* PSD_LAYER_DECODE_H */
