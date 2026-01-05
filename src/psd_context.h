/**
 * @file psd_context.h
 * @brief Internal document context structure
 *
 * Exposes the internal document structure definition for use by internal modules.
 * This is NOT part of the public API.
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

#ifndef PSD_CONTEXT_H
#define PSD_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>
#include "psd_alloc.h"
#include "psd_composite.h"
#include "psd_descriptor.h"
#include "psd_header.h"
#include "psd_layer.h"
#include "psd_text_layer.h"
#include "psd_resources.h"
#include "../include/openpsd/psd_types.h"

/**
 * @brief PSD document structure (internal representation)
 *
 * All document state is kept in this structure. No global state.
 */
struct psd_document {
    const psd_allocator_t *allocator; /**< Allocator used for this document */
    bool is_psb;                      /**< True if PSB (v2) format, false if PSD (v1) */
    uint32_t width;                   /**< Image width in pixels */
    uint32_t height;                  /**< Image height in pixels */
    uint16_t channels;                /**< Number of color channels */
    uint16_t depth;                   /**< Bit depth (1, 8, 16, or 32) */
    psd_color_mode_t color_mode;      /**< Color mode */
    psd_color_mode_data_t color_data; /**< Color mode data (palette, etc.) */
    psd_resources_t resources;        /**< Image resources section */
    psd_layer_info_t layers;          /**< Layer and mask information */
    psd_composite_image_t composite;  /**< Composite image data */

    psd_text_layer_info_t text_layers; /**< Text layer information */
};

#endif /* PSD_CONTEXT_H */
