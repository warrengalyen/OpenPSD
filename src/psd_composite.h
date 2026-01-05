/**
 * @file psd_composite.h
 * @brief Composite Image Data section handling
 *
 * The Composite Image Data section contains the final, flattened image.
 * It can be compressed with RAW, RLE, ZIP, or ZIP with prediction.
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

#ifndef PSD_COMPOSITE_H
#define PSD_COMPOSITE_H

#include <stdint.h>
#include <stddef.h>
#include "../include/openpsd/psd_types.h"

/**
 * @brief Composite image data
 *
 * Stores the final composite image as planar channel data.
 * If depth is 8 bits and channels is 3 (RGB), data is organized as:
 *  - First height*width bytes: all red channel pixels
 *  - Next height*width bytes: all green channel pixels
 *  - Next height*width bytes: all blue channel pixels
 */
typedef struct {
    uint8_t *data;              /**< Raw image data (planar layout) */
    uint64_t data_length;       /**< Total length of data */
    psd_compression_t compression;  /**< Compression type */
} psd_composite_image_t;

#endif /* PSD_COMPOSITE_H */
