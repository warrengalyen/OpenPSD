/**
 * @file psd_layer.h
 * @brief Internal Layer and Mask Information structures
 *
 * Defines the in-memory representation of PSD layer records and layer information.
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

#ifndef PSD_LAYER_H
#define PSD_LAYER_H

#include <stdint.h>
#include <stdbool.h>
#include "psd_layer_channel.h"
#include "psd_descriptor.h"
#include "../include/openpsd/psd_types.h"

/**
 * @brief Bounding rectangle for a layer
 *
 * Represents the position and size of a layer in the image.
 */
typedef struct {
    int32_t top;      /**< Top coordinate */
    int32_t left;     /**< Left coordinate */
    int32_t bottom;   /**< Bottom coordinate */
    int32_t right;    /**< Right coordinate */
} psd_layer_bounds_t;

/**
 * @brief A single layer record
 *
 * Contains all information about one layer in the PSD file.
 * Channel pixel data is not decoded by default - use accessors to decode on demand.
 */
typedef struct {
    psd_layer_bounds_t bounds;           /**< Layer bounding box */
    psd_layer_channel_data_t *channels;  /**< Array of channel information with lazy decoding */
    size_t channel_count;                /**< Number of channels */
    uint32_t blend_sig;                  /**< Blend mode signature (usually "8BIM") */
    uint32_t blend_key;                  /**< Blend mode key (e.g. "norm", "scrn") */
    uint8_t opacity;                     /**< Opacity 0-255 */
    uint8_t clipping;                    /**< Clipping mask 0=base 1=non-base */
    uint8_t flags;                       /**< Layer flags */
    uint8_t *name;                       /**< Layer name (Unicode, NULL-terminated) */
    size_t name_length;                  /**< Length of name in bytes */
    uint8_t *additional_data;            /**< Raw additional layer info blocks */
    uint64_t additional_length;          /**< Length of additional info */
    psd_descriptor_t *descriptor;        /**< Layer descriptor (effects, text, etc.) - may be NULL */
    psd_layer_features_t features;       /**< Layer features detected from additional info */
} psd_layer_record_t;

/**
 * @brief Layer and Mask Information section
 *
 * Contains all layer records and additional layer information.
 * Transparency layer (negative count) is counted but marked separately.
 */
typedef struct {
    psd_layer_record_t *layers;  /**< Array of layer records */
    int32_t layer_count;         /**< Number of layers (negative if transparency layer present) */
    bool has_transparency_layer; /**< True if negative count indicated transparency layer */
} psd_layer_info_t;

#endif /* PSD_LAYER_H */
