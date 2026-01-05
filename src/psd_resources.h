/**
 * @file psd_resources.h
 * @brief Internal Image Resources section structures
 *
 * Defines the in-memory representation of PSD Image Resources.
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

#ifndef PSD_RESOURCES_H
#define PSD_RESOURCES_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Known resource IDs
 *
 * These are well-known resource IDs that we recognize.
 * Unknown IDs are preserved as raw data and never cause parsing to fail.
 */
typedef enum {
    PSD_RESOURCE_RESOLUTION = 1005,         /**< Print settings and resolution */
    PSD_RESOURCE_THUMBNAIL = 1033,          /**< Thumbnail image (old format) */
    PSD_RESOURCE_THUMBNAIL_2 = 1036,        /**< Thumbnail image (new format) */
    PSD_RESOURCE_ICC_PROFILE = 1039,        /**< Embedded ICC color profile */
    PSD_RESOURCE_XMP = 1060,                /**< XMP metadata */
} psd_resource_id_t;

/**
 * @brief A single Image Resource block
 *
 * Each resource block in the Image Resources section has:
 * - Signature ("8BIM" or "8B64")
 * - Resource ID
 * - Pascal string name
 * - Resource data
 *
 * We preserve raw data for all resources, including unknown ones.
 */
typedef struct {
    uint16_t id;              /**< Resource ID (e.g., 1005 for resolution) */
    uint8_t *name;            /**< Pascal string name (NULL for empty name) */
    size_t name_length;       /**< Length of name string */
    uint8_t *data;            /**< Raw resource data */
    uint64_t data_length;     /**< Length of resource data */
} psd_resource_block_t;

/**
 * @brief Image Resources section
 *
 * Contains an array of resource blocks.
 * Unknown resources are preserved exactly as found.
 */
typedef struct {
    psd_resource_block_t *blocks;  /**< Array of resource blocks */
    size_t count;                  /**< Number of resource blocks */
} psd_resources_t;

#endif /* PSD_RESOURCES_H */
