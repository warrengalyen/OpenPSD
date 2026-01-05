/**
 * @file psd_context.c
 * @brief Document context and state management
 *
 * Manages the parsing context and document state.
 * No global state - all state is encapsulated in the document structure.
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

#include "../include/openpsd/psd.h"
#include "../include/openpsd/psd_stream.h"
#include "psd_context.h"
#include "psd_alloc.h"
#include "psd_composite.h"
#include "psd_descriptor.h"
#include "psd_endian.h"
#include "psd_header.h"
#include "psd_layer.h"
#include "psd_text_layer.h"
#include "psd_text_layer_parse.h"
#include "psd_layer_channel.h"
#include "psd_layer_decode.h"
#include "psd_resources.h"
#include "psd_unicode.h"
#include "psd_rle.h"
#include "psd_zip.h"
#include <stdbool.h>
#include <string.h>

#include <stdio.h>

/* Forward declarations for parsing functions */
static psd_status_t psd_parse_composite_image(psd_stream_t *stream,
                                              psd_document_t *doc);

static psd_status_t psd_try_decode_composite_rle(
    psd_stream_t *stream,
    int64_t counts_pos,
    uint32_t num_scanlines,
    uint64_t bytes_per_scanline,
    uint8_t *dst,
    uint64_t expected_uncompressed_size,
    const psd_allocator_t *alloc,
    uint32_t count_bytes)
{
    psd_status_t st;
    uint64_t compressed_size64 = 0;

    if (psd_stream_seek(stream, counts_pos) < 0) {
        return PSD_ERR_STREAM_INVALID;
    }

    if (count_bytes == 2) {
        for (uint32_t i = 0; i < num_scanlines; i++) {
            uint16_t v16 = 0;
            st = psd_stream_read_be16(stream, &v16);
            if (st != PSD_OK) return st;
            compressed_size64 += (uint64_t)v16;
        }
    } else if (count_bytes == 4) {
        for (uint32_t i = 0; i < num_scanlines; i++) {
            uint32_t v32 = 0;
            st = psd_stream_read_be32(stream, &v32);
            if (st != PSD_OK) return st;
            compressed_size64 += (uint64_t)v32;
        }
    } else {
        return PSD_ERR_INVALID_ARGUMENT;
    }

    size_t compressed_size = 0;
    if (psd_u64_to_size(compressed_size64, &compressed_size) != 0) {
        return PSD_ERR_OUT_OF_RANGE;
    }

    uint8_t *compressed_data = (uint8_t *)psd_alloc_malloc(alloc, compressed_size);
    if (!compressed_data) {
        return PSD_ERR_OUT_OF_MEMORY;
    }

    int64_t read_bytes = psd_stream_read(stream, compressed_data, compressed_size);
    if (read_bytes < 0 || (size_t)read_bytes != compressed_size) {
        psd_alloc_free(alloc, compressed_data);
        return PSD_ERR_STREAM_EOF;
    }

    size_t scanline_w = 0;
    if (psd_u64_to_size(bytes_per_scanline, &scanline_w) != 0) {
        psd_alloc_free(alloc, compressed_data);
        return PSD_ERR_OUT_OF_RANGE;
    }

    size_t out_len = 0;
    st = psd_rle_decode(
        compressed_data,
        compressed_size,
        (size_t)num_scanlines,
        scanline_w,
        dst,
        &out_len);

    psd_alloc_free(alloc, compressed_data);

    if (st != PSD_OK) return st;
    if ((uint64_t)out_len != expected_uncompressed_size) return PSD_ERR_CORRUPT_DATA;
    return PSD_OK;
}

/**
 * @brief Parse PSD file header
 *
 * Validates the PSD signature and reads basic header information.
 * The stream must be positioned at the start of the file.
 *
 * Supports both PSD (v1) and PSB/Large Document (v2) formats.
 * Sets is_psb flag based on version.
 *
 * @param stream Stream to read from
 * @param doc Document to populate with header data
 * @return PSD_OK on success, negative error code on failure
 */
static psd_status_t psd_parse_header(psd_stream_t *stream,
                                     psd_document_t *doc) {
    psd_status_t status;
    uint32_t signature;
    uint16_t version;

    /* Read and validate signature */
    status = psd_stream_read_be32(stream, &signature);
    if (status != PSD_OK) {
        return status;
    }

    /* PSD signature must be "8BPS" (0x38425053) */
    if (signature != PSD_SIGNATURE) {
        return PSD_ERR_INVALID_FILE_FORMAT;
    }

    /* Read version */
    status = psd_stream_read_be16(stream, &version);
    if (status != PSD_OK) {
        return status;
    }

    /* Version must be 1 (PSD) or 2 (PSB/Large Document) */
    if (version != PSD_VERSION_PSD && version != PSD_VERSION_PSB) {
        return PSD_ERR_UNSUPPORTED_VERSION;
    }

    /* Set PSB flag based on version */
    doc->is_psb = (version == PSD_VERSION_PSB);

    /* Read and skip 6 reserved bytes (required by PSD format) */
    uint8_t reserved_bytes[6];
    status = psd_stream_read_exact(stream, reserved_bytes, 6);
    if (status != PSD_OK) {
        return status;
    }

    /* Read number of channels (1-56) */
    status = psd_stream_read_be16(stream, &doc->channels);
    if (status != PSD_OK) {
        return status;
    }

    /* Validate channels */
    if (doc->channels < 1 || doc->channels > PSD_MAX_CHANNELS) {
        return PSD_ERR_INVALID_HEADER;
    }

    /* Read image height */
    status = psd_stream_read_be32(stream, &doc->height);
    if (status != PSD_OK) {
        return status;
    }

    /* Read image width */
    status = psd_stream_read_be32(stream, &doc->width);
    if (status != PSD_OK) {
        return status;
    }

    /* Validate dimensions */
    uint32_t max_dim =
        doc->is_psb ? PSD_MAX_DIMENSION_PSB : PSD_MAX_DIMENSION_STANDARD;
    if (doc->width < 1 || doc->width > max_dim || doc->height < 1 ||
        doc->height > max_dim) {
        return PSD_ERR_INVALID_HEADER;
    }

    /* Read depth (bits per channel) */
    status = psd_stream_read_be16(stream, &doc->depth);
    if (status != PSD_OK) {
        return status;
    }

    /* Validate depth - allow 1, 8, 16, 32 */
    if (doc->depth != 1 && doc->depth != 8 && doc->depth != 16 &&
        doc->depth != 32) {
        return PSD_ERR_INVALID_HEADER;
    }

    /* Read color mode - store as-is (don't reject newer modes) */
    uint16_t color_mode_raw;
    status = psd_stream_read_be16(stream, &color_mode_raw);
    if (status != PSD_OK) {
        return status;
    }

    doc->color_mode = (psd_color_mode_t)color_mode_raw;

    return PSD_OK;
}

/**
 * @brief Parse Color Mode Data section
 *
 * Reads the Color Mode Data section which may contain:
 * - Palette data for indexed color mode (3 bytes * 256 = 768 bytes)
 * - Duotone information for duotone mode
 * - Empty data for most other modes
 *
 * This function preserves all raw data without interpretation.
 * Unknown or future color modes are handled gracefully.
 *
 * @param stream Stream to read from
 * @param doc Document to populate with color mode data
 * @return PSD_OK on success, negative error code on failure
 */
static psd_status_t psd_parse_color_mode_data(psd_stream_t *stream,
                                              psd_document_t *doc) {
    psd_status_t status;
    uint64_t data_length;

    /* Initialize to empty */
    doc->color_data.data = NULL;
    doc->color_data.length = 0;

    /* Color Mode Data section length is 4 bytes in both PSD and PSB */
    uint32_t data_length32 = 0;
    status = psd_stream_read_be32(stream, &data_length32);
    if (status != PSD_OK) {
        return status;
    }
    data_length = (uint64_t)data_length32;

    /* If no color data, we're done */
    if (data_length == 0) {
        return PSD_OK;
    }

    /* Convert uint64_t length to size_t for allocation (with overflow check) */
    size_t data_size;
    if (psd_u64_to_size(data_length, &data_size) != 0) {
        /* Length is too large to allocate */
        return PSD_ERR_OUT_OF_RANGE;
    }

    /* Allocate buffer for color mode data */
    doc->color_data.data =
        (uint8_t *)psd_alloc_malloc(doc->allocator, data_size);
    if (!doc->color_data.data) {
        return PSD_ERR_OUT_OF_MEMORY;
    }

    /* Read the color mode data */
    status = psd_stream_read_exact(stream, doc->color_data.data, data_size);
    if (status != PSD_OK) {
        /* Free the buffer on error */
        psd_alloc_free(doc->allocator, doc->color_data.data);
        doc->color_data.data = NULL;
        doc->color_data.length = 0;
        return status;
    }

    /* Store the actual length */
    doc->color_data.length = data_length;

    return PSD_OK;
}

/**
 * @brief Parse Image Resources section
 *
 * Reads the Image Resources section which contains metadata like:
 * - Print settings and resolution (1005)
 * - Thumbnails (1033, 1036)
 * - ICC color profile (1039)
 * - XMP metadata (1060)
 * - And many other resource types
 *
 * Unknown resource IDs are preserved as raw data without interpretation.
 * This ensures forward compatibility with future Adobe Photoshop versions.
 *
 * @param stream Stream to read from
 * @param doc Document to populate with resources
 * @return PSD_OK on success, negative error code on failure
 */
static psd_status_t psd_parse_resources(psd_stream_t *stream,
                                        psd_document_t *doc) {
    psd_status_t status;
    uint64_t section_length;

    /* Initialize resources */
    doc->resources.blocks = NULL;
    doc->resources.count = 0;

    /* Image Resources section length is 4 bytes in both PSD and PSB */
    uint32_t section_length32 = 0;
    status = psd_stream_read_be32(stream, &section_length32);
    if (status != PSD_OK) {
        return status;
    }
    section_length = (uint64_t)section_length32;

    /* If section is empty, we're done */
    if (section_length == 0) {
        return PSD_OK;
    }

    /* Get the current position to track section boundaries */
    int64_t section_start = psd_stream_tell(stream);
    if (section_start < 0) {
        return (psd_status_t)section_start;
    }

    int64_t section_end = section_start + (int64_t)section_length;

    /* Parse resource blocks until end of section */
    size_t block_capacity = 16; /* Start with capacity for 16 blocks */
    size_t block_count = 0;

    psd_resource_block_t *blocks = (psd_resource_block_t *)psd_alloc_malloc(
        doc->allocator, block_capacity * sizeof(psd_resource_block_t));
    if (!blocks) {
        return PSD_ERR_OUT_OF_MEMORY;
    }

    while (psd_stream_tell(stream) < section_end) {
        uint32_t signature;
        uint16_t resource_id;
        uint8_t pascal_length;
        uint64_t data_length;

        /* Read signature (should be "8BIM" or "8B64") */
        status = psd_stream_read_be32(stream, &signature);
        if (status != PSD_OK) {
            goto error;
        }

        if (signature != 0x3842494D &&
            signature != 0x38423634) { /* "8BIM" and "8B64" */
            /* Not a valid resource block signature.
             *
             * Some writers may pad or include non-standard data. Image resources are
             * optional metadata; if we can't parse further blocks safely, stop here
             * and seek to the end of the section to keep the stream aligned.
             */
            psd_stream_seek(stream, section_end);
            break;
        }

        /* Read resource ID */
        status = psd_stream_read_be16(stream, &resource_id);
        if (status != PSD_OK) {
            goto error;
        }

        /* Read Pascal string name length (1 byte) */
        status = psd_stream_read_exact(stream, &pascal_length, 1);
        if (status != PSD_OK) {
            goto error;
        }

        uint8_t *name = NULL;
        size_t name_length = pascal_length;

        if (pascal_length > 0) {
            name = (uint8_t *)psd_alloc_malloc(doc->allocator, pascal_length);
            if (!name) {
                status = PSD_ERR_OUT_OF_MEMORY;
                goto error;
            }

            status = psd_stream_read_exact(stream, name, pascal_length);
            if (status != PSD_OK) {
                psd_alloc_free(doc->allocator, name);
                goto error;
            }
        }

        /* Pad name to even boundary (includes the 1-byte length) */
        if (((1u + (uint32_t)pascal_length) & 1u) != 0u) {
            status = psd_stream_skip(stream, 1);
            if (status != PSD_OK) {
                psd_alloc_free(doc->allocator, name);
                goto error;
            }
        }

        /* Read resource data length (always 4 bytes, even in PSB) */
        uint32_t data_length32 = 0;
        status = psd_stream_read_be32(stream, &data_length32);
        if (status != PSD_OK) {
            psd_alloc_free(doc->allocator, name);
            goto error;
        }
        data_length = (uint64_t)data_length32;

        /* Allocate data buffer */
        uint8_t *data = NULL;
        if (data_length > 0) {
            size_t data_size;
            if (psd_u64_to_size(data_length, &data_size) != 0) {
                psd_alloc_free(doc->allocator, name);
                status = PSD_ERR_OUT_OF_RANGE;
                goto error;
            }

            data = (uint8_t *)psd_alloc_malloc(doc->allocator, data_size);
            if (!data) {
                psd_alloc_free(doc->allocator, name);
                status = PSD_ERR_OUT_OF_MEMORY;
                goto error;
            }

            status = psd_stream_read_exact(stream, data, data_size);
            if (status != PSD_OK) {
                psd_alloc_free(doc->allocator, name);
                psd_alloc_free(doc->allocator, data);
                goto error;
            }
        }

        /* Pad data to even boundary */
        if (data_length % 2 != 0) {
            status = psd_stream_skip(stream, 1);
            if (status != PSD_OK) {
                psd_alloc_free(doc->allocator, name);
                psd_alloc_free(doc->allocator, data);
                goto error;
            }
        }

        /* Grow block array if needed */
        if (block_count >= block_capacity) {
            block_capacity *= 2;
            psd_resource_block_t *new_blocks =
                (psd_resource_block_t *)psd_alloc_realloc(
                    doc->allocator, blocks,
                    block_capacity * sizeof(psd_resource_block_t));
            if (!new_blocks) {
                psd_alloc_free(doc->allocator, name);
                psd_alloc_free(doc->allocator, data);
                status = PSD_ERR_OUT_OF_MEMORY;
                goto error;
            }
            blocks = new_blocks;
        }

        /* Store resource block */
        blocks[block_count].id = resource_id;
        blocks[block_count].name = name;
        blocks[block_count].name_length = name_length;
        blocks[block_count].data = data;
        blocks[block_count].data_length = data_length;
        block_count++;
    }

    doc->resources.blocks = blocks;
    doc->resources.count = block_count;

    /* Verify we're at the end of the section */
    int64_t final_pos = psd_stream_tell(stream);
    if (final_pos < 0) {
        return (psd_status_t)final_pos;
    }

    /* If we're not at section_end, we might have a parsing issue */
    /* Try to seek to the correct position if we're off */
    if (final_pos != section_end) {
        /* Seek to expected end position */
        int64_t seek_result = psd_stream_seek(stream, section_end);
        if (seek_result < 0) {
            /* Can't seek - this is a problem */
            return (psd_status_t)seek_result;
        }
    }

    return PSD_OK;

error:
    /* Free all allocated blocks on error */
    for (size_t i = 0; i < block_count; i++) {
        psd_alloc_free(doc->allocator, blocks[i].name);
        psd_alloc_free(doc->allocator, blocks[i].data);
    }
    psd_alloc_free(doc->allocator, blocks);

    return status;
}

/**
 * @brief Parse Layer and Mask Information section
 *
 * Reads the complete layer and mask information including:
 * - Layer records (bounds, channels, blend modes, names)
 * - Channel information (not decoded)
 * - Additional layer info blocks (preserved as-is)
 *
 * Handles transparency layers (negative layer count) correctly.
 *
 * @param stream Stream to read from
 * @param doc Document to populate with layer information
 * @return PSD_OK on success, negative error code on failure
 */
static psd_status_t psd_parse_layer_info(psd_stream_t *stream, psd_document_t *doc) {
    psd_status_t status;
    uint64_t section_length;

    /* Initialize layers */
    doc->layers.layers = NULL;
    doc->layers.layer_count = 0;
    doc->layers.has_transparency_layer = false;

    /* Read section length.
     *
     * PSB typically uses 8-byte lengths, but some writers may still emit 4-byte
     * lengths here. We probe the computed end with a seek and fall back to 4-byte
     * if it is not plausible.
     */
    int64_t section_len_pos = psd_stream_tell(stream);
    if (section_len_pos < 0) {
        return (psd_status_t)section_len_pos;
    }

    status = psd_stream_read_length(stream, doc->is_psb, &section_length);
    if (status != PSD_OK) {
        return status;
    }

    /* If section is empty, we're done */
    if (section_length == 0) {
        return PSD_OK;
    }

    /* Get the current position to track section boundaries (used for
     * validation)
     */
    int64_t section_start = psd_stream_tell(stream);
    if (section_start < 0) {
        return (psd_status_t)section_start;
    }

    int64_t section_end = section_start + (int64_t)section_length;
    if (doc->is_psb) {
        int64_t probe = psd_stream_seek(stream, section_end);
        if (probe < 0) {
            /* Fall back to 4-byte section length */
            if (psd_stream_seek(stream, section_len_pos) < 0) {
                return PSD_ERR_STREAM_INVALID;
            }
            uint32_t section_len32 = 0;
            status = psd_stream_read_be32(stream, &section_len32);
            if (status != PSD_OK) {
                return status;
            }
            section_length = (uint64_t)section_len32;
            section_start = psd_stream_tell(stream);
            if (section_start < 0) {
                return (psd_status_t)section_start;
            }
            section_end = section_start + (int64_t)section_length;
        } else {
            /* Restore stream position */
            if (psd_stream_seek(stream, section_start) < 0) {
                return PSD_ERR_STREAM_INVALID;
            }
        }
    }

    /* Layer Info subsection length.
     *
     * Spec: PSD uses 4 bytes. PSB typically uses 8 bytes, but some real-world files
     * may still store a 4-byte length here. We try the PSB width first and fall back
     * to 4-byte if the result is not plausible within section_end.
     */
    int64_t layer_info_len_pos = psd_stream_tell(stream);
    if (layer_info_len_pos < 0) {
        return (psd_status_t)layer_info_len_pos;
    }

    uint64_t layer_info_length64 = 0;
    status = psd_stream_read_length(stream, doc->is_psb, &layer_info_length64);
    if (status != PSD_OK) {
        return status;
    }

    int64_t layer_info_start = psd_stream_tell(stream);
    if (layer_info_start < 0) {
        return (psd_status_t)layer_info_start;
    }
    int64_t layer_info_end = layer_info_start + (int64_t)layer_info_length64;

    /* Basic sanity: layer info must fit within the overall section.
     * If not, try a 4-byte length fallback (seen in some PSB writers). */
    if (layer_info_end > section_end) {
        if (doc->is_psb) {
            if (psd_stream_seek(stream, layer_info_len_pos) < 0) {
                return PSD_ERR_STREAM_INVALID;
            }
            uint32_t layer_info_len32 = 0;
            status = psd_stream_read_be32(stream, &layer_info_len32);
            if (status != PSD_OK) {
                return status;
            }
            layer_info_length64 = (uint64_t)layer_info_len32;
            layer_info_start = psd_stream_tell(stream);
            if (layer_info_start < 0) {
                return (psd_status_t)layer_info_start;
            }
            layer_info_end = layer_info_start + (int64_t)layer_info_length64;
        }
    }

    if (layer_info_end > section_end) {
        return PSD_ERR_CORRUPT_DATA;
    }

    /* Use section_start for validation - don't suppress */

    /* Read layer count */
    int16_t raw_layer_count;
    status = psd_stream_read_be16(stream, (uint16_t *)&raw_layer_count);
    if (status != PSD_OK) {
        return status;
    }

    /* Handle negative count (indicates transparency layer) */
    int32_t layer_count = raw_layer_count;
    if (layer_count < 0) {
        doc->layers.has_transparency_layer = true;
        layer_count = -layer_count;
    }

    /* Allocate layer array */
    if (layer_count > 0) {
        doc->layers.layers = (psd_layer_record_t *)psd_alloc_malloc(
            doc->allocator, layer_count * sizeof(psd_layer_record_t));
        if (!doc->layers.layers) {
            return PSD_ERR_OUT_OF_MEMORY;
        }

        /* Initialize all layers to zero */
        for (int32_t i = 0; i < layer_count; i++) {
            doc->layers.layers[i].bounds.top = 0;
            doc->layers.layers[i].bounds.left = 0;
            doc->layers.layers[i].bounds.bottom = 0;
            doc->layers.layers[i].bounds.right = 0;
            doc->layers.layers[i].channels = NULL;
            doc->layers.layers[i].channel_count = 0;
            doc->layers.layers[i].blend_sig = 0;
            doc->layers.layers[i].blend_key = 0;
            doc->layers.layers[i].opacity = 255;
            doc->layers.layers[i].clipping = 0;
            doc->layers.layers[i].flags = 0;
            doc->layers.layers[i].name = NULL;
            doc->layers.layers[i].name_length = 0;
            doc->layers.layers[i].additional_data = NULL;
            doc->layers.layers[i].additional_length = 0;
            doc->layers.layers[i].descriptor = NULL;
            /* Initialize features to all false */
            memset(&doc->layers.layers[i].features, 0,
                   sizeof(psd_layer_features_t));
        }
    }

    /* Parse layer records */
    for (int32_t i = 0; i < layer_count; i++) {
        psd_layer_record_t *layer = &doc->layers.layers[i];

        /* Read bounding box */
        status = psd_stream_read_be_i32(stream, &layer->bounds.top);
        if (status != PSD_OK) {
            goto error;
        }
        status = psd_stream_read_be_i32(stream, &layer->bounds.left);
        if (status != PSD_OK)
            goto error;
        status = psd_stream_read_be_i32(stream, &layer->bounds.bottom);
        if (status != PSD_OK)
            goto error;
        status = psd_stream_read_be_i32(stream, &layer->bounds.right);
        if (status != PSD_OK) {
            goto error;
        }

        /* Validate bounds are reasonable */
        /* Check for obviously invalid bounds that suggest file corruption or
         * misalignment */
        bool bounds_invalid = false;

        /* Check if bounds are unreasonably large (likely corruption) */
        if (layer->bounds.top > 1000000 || layer->bounds.left > 1000000 ||
            layer->bounds.bottom > 1000000 || layer->bounds.right > 1000000 ||
            layer->bounds.top < -1000000 || layer->bounds.left < -1000000 ||
            layer->bounds.bottom < -1000000 || layer->bounds.right < -1000000) {
            bounds_invalid = true;
        }

        /* Check if bounds make logical sense (bottom >= top, right >= left) */
        if (!bounds_invalid) {
            if (layer->bounds.bottom < layer->bounds.top ||
                layer->bounds.right < layer->bounds.left) {
                bounds_invalid = true;
            }
        }

        /* Check for the specific suspicious pattern: right=height, top=huge,
         * left=0, bottom=0 */
        /* This pattern indicates we're reading image dimensions instead of
         * layer bounds */
        /* However, a valid full-image layer can have top=0, left=0,
         * bottom=height, right=width */
        /* So we only flag as invalid if top is unreasonably large (misalignment
         * indicator) */
        if (!bounds_invalid && i == 0 &&
            layer->bounds.right == (int32_t)doc->height &&
            layer->bounds.top > 1000000 && layer->bounds.left == 0 &&
            layer->bounds.bottom == 0) {
            bounds_invalid = true;
        }

        /* A full-image layer with top=0, left=0, bottom=height, right=width is
         * valid */
        /* Don't flag it as invalid just because right matches height */
        if (!bounds_invalid && layer->bounds.top == 0 &&
            layer->bounds.left == 0 &&
            layer->bounds.bottom == (int32_t)doc->height &&
            layer->bounds.right == (int32_t)doc->width) {
            /* This is a valid full-image layer - bounds are correct */
            bounds_invalid = false;
        }

        if (bounds_invalid) {
            /* Don't reset bounds to 0 - keep the original values even if they
             * look invalid */
            /* This helps with debugging and allows the caller to see what was
             * actually in the file */
            /* The bounds might be invalid due to unsupported layer types (text,
             * smart objects, etc.) */
        }

        /* Read number of channels */
        uint16_t channel_count;

        status = psd_stream_read_be16(stream, &channel_count);
        if (status != PSD_OK) {
            goto error;
        }

        /* Validate channel count - if invalid, treat as empty layer */
        if (channel_count > 56) {
            /* Invalid channel count suggests misalignment - treat as empty
             * layer */
            channel_count = 0;
        }

        /* Allocate channels */
        layer->channel_count = channel_count;
        if (channel_count > 0) {
            layer->channels = (psd_layer_channel_data_t *)psd_alloc_malloc(
                doc->allocator,
                channel_count * sizeof(psd_layer_channel_data_t));
            if (!layer->channels) {
                status = PSD_ERR_OUT_OF_MEMORY;
                goto error;
            }

            /* Read channel descriptors (ID + length only - pixel data is stored
             * separately) */
            for (uint16_t j = 0; j < channel_count; j++) {
                int16_t id;
                uint64_t length;

                /* Read signed channel ID*/
                uint16_t tmp;
                status = psd_stream_read_be16(stream, &tmp);
                if (status != PSD_OK) {
                    goto error;
                }
                id = (int16_t)tmp;

                /* Read channel data length */
                int64_t chan_len_pos = psd_stream_tell(stream);
                if (chan_len_pos < 0) {
                    status = (psd_status_t)chan_len_pos;
                    goto error;
                }

                status = psd_stream_read_length(stream, doc->is_psb, &length);
                if (status != PSD_OK) {
                    goto error;
                }

                /* PSB typically uses 8-byte channel lengths, but some files may
                 * still store 4-byte lengths. If the parsed length is implausible
                 * within the Layer Info subsection bounds, fall back to 4 bytes. */
                if (doc->is_psb) {
                    int64_t after_len_pos = psd_stream_tell(stream);
                    if (after_len_pos < 0) {
                        status = (psd_status_t)after_len_pos;
                        goto error;
                    }
                    int64_t remaining_in_layer_info = layer_info_end - after_len_pos;
                    if (remaining_in_layer_info > 0 &&
                        length > (uint64_t)remaining_in_layer_info) {
                        if (psd_stream_seek(stream, chan_len_pos) < 0) {
                            status = PSD_ERR_STREAM_INVALID;
                            goto error;
                        }
                        uint32_t len32 = 0;
                        status = psd_stream_read_be32(stream, &len32);
                        if (status != PSD_OK) {
                            goto error;
                        }
                        length = (uint64_t)len32;
                    }
                }

                if (!doc->is_psb && length > 0xFFFFFFFFu) {
                    status = PSD_ERR_CORRUPT_DATA;
                    goto error;
                }

                /* Initialize channel structure */
                /* NOTE: Channel image data (compression type + pixel data) is
                   stored AFTER all layer records, not as part of each layer
                   record. We only store the channel info here. */
                layer->channels[j].channel_id = id;
                layer->channels[j].compressed_length = length;
                layer->channels[j].is_decoded = false;
                layer->channels[j].decoded_data = NULL;
                layer->channels[j].decoded_length = 0;
                layer->channels[j].compression = 0;
                layer->channels[j].compressed_data = NULL;
            }
        }

        /* Read blend mode signature */
        status = psd_stream_read_be32(stream, &layer->blend_sig);
        if (status != PSD_OK)
            goto error;

        /* Read blend mode key */
        status = psd_stream_read_be32(stream, &layer->blend_key);
        if (status != PSD_OK)
            goto error;

        /* Validate blend signature - should be "8BIM" or "8B64" */
        if (layer->blend_sig != 0x3842494D && layer->blend_sig != 0x38423634) {
            /* If bounds were also invalid, this confirms misalignment - treat
             * as empty layer */
            if (bounds_invalid) {
                /* Reset blend mode to safe defaults */
                layer->blend_sig = 0x3842494D; /* "8BIM" */
                layer->blend_key = 0x6E6F726D; /* "norm" */
            }
        }

        /* Read opacity (1 byte) */
        uint8_t byte_val;
        status = psd_stream_read_exact(stream, &byte_val, 1);
        if (status != PSD_OK)
            goto error;
        layer->opacity = byte_val;

        /* Read clipping (1 byte) */
        status = psd_stream_read_exact(stream, &byte_val, 1);
        if (status != PSD_OK)
            goto error;
        layer->clipping = byte_val;

        /* Read flags (1 byte) */
        status = psd_stream_read_exact(stream, &byte_val, 1);
        if (status != PSD_OK)
            goto error;
        layer->flags = byte_val;

        /* Read filler byte (must be 0) */
        uint8_t filler_byte;
        status = psd_stream_read_exact(stream, &filler_byte, 1);
        if (status != PSD_OK)
            goto error;

        /* Read extra layer information length (4 bytes) */
        /* According to Adobe spec: this is the total length of:
           1. Layer mask data (4 bytes length + variable data)
           2. Layer blending ranges (4 bytes length + variable data)
           3. Layer name (Pascal string, padded to multiple of 4)
           4. Additional layer information blocks (tagged blocks) */
        uint32_t extra_length;
        uint8_t extra_len_bytes[4];
        status = psd_stream_read_exact(stream, extra_len_bytes, 4);
        if (status != PSD_OK) {
            goto error;
        }
        extra_length = ((uint32_t)extra_len_bytes[0] << 24) |
                       ((uint32_t)extra_len_bytes[1] << 16) |
                       ((uint32_t)extra_len_bytes[2] << 8) |
                       ((uint32_t)extra_len_bytes[3]);

        /* Check if this is an unsupported layer type based on extra_length */
        /* Unreasonably large extra_length indicates we're misaligned or reading
         * channel image data instead of layer extra data. Normal layer extra
         * data
         * is typically < 100KB. Anything > 1MB is almost certainly wrong. */
        if (extra_length >
            1000000) { /* > 1MB is definitely wrong - likely misalignment */
            /* Mark as empty and skip trying to read extra data */
            layer->channel_count = 0;
            if (layer->channels) {
                psd_alloc_free(doc->allocator, layer->channels);
                layer->channels = NULL;
            }
            layer->bounds.top = 0;
            layer->bounds.left = 0;
            layer->bounds.bottom = 0;
            layer->bounds.right = 0;

            /* CRITICAL: Even though we're not parsing the extra data, we MUST
             * skip it in the stream, or we'll be misaligned for the next layer.
             * However, if skipping would take us past section_end, we've likely
             * already read into the channel image data section, so we should
             * stop here. */
            int64_t current_pos = psd_stream_tell(stream);
            if (current_pos < 0) {
                status = (psd_status_t)current_pos;
                goto error;
            }
            if (current_pos + (int64_t)extra_length > section_end) {
                /* We've likely read into channel image data - stop parsing
                 * layers and seek to section_end */
                status = psd_stream_seek(stream, section_end);
                if (status < 0) {
                    goto error;
                }
                /* Mark remaining layers as not parsed */
                layer_count = i + 1;
                break; /* Exit the layer parsing loop */
            }

            status = psd_stream_skip(stream, extra_length);
            if (status != PSD_OK) {
                goto error;
            }
            /* Don't try to parse extra data */
            extra_length = 0;
        }

        /* Store additional layer info (but don't parse it yet) */
        /* According to spec, extra data contains:
           1. Layer mask data (4 bytes length + variable data)
           2. Layer blending ranges (4 bytes length + variable data)
           3. Layer name (Pascal string, padded to multiple of 4)
           4. Additional layer information blocks (tagged blocks)
           The extra_length field specifies the total length of all these fields
         */

        /* Read extra data ONLY if length is reasonable */
        /* Skip if unreasonably large (indicates unsupported or corrupted layer)
         */
        /* Most normal layers have < 10MB of extra data; anything larger is
         * likely unsupported */

        /* IMPORTANT: Even if extra_length is 0, we may still need to detect
           group
           markers that come from the layer name (legacy Photoshop format) */

        if (extra_length > 0 && extra_length <= 10000000) {
            layer->additional_data =
                (uint8_t *)psd_alloc_malloc(doc->allocator, extra_length);
            if (!layer->additional_data) {
                status = PSD_ERR_OUT_OF_MEMORY;
                goto error;
            }

            layer->additional_length = extra_length;

            status = psd_stream_read_exact(stream, layer->additional_data,
                                           extra_length);
            if (status != PSD_OK) {
                goto error;
            }

            /* Parse additional layer information blocks to detect layer type */
            /* The extra data contains: layer mask data, blending ranges, layer
             * name, and additional layer info blocks */
            /* We'll look for keys that identify unsupported layer types */
            if (layer->additional_data && layer->additional_length > 0) {
                /* Scan for unsupported layer type keys */
                uint8_t *data = layer->additional_data;
                uint64_t remaining = layer->additional_length;
                uint8_t *data_end = data + remaining;

                /* Skip layer mask data (starts with 4-byte length) */
                if (remaining >= 4) {
                    uint32_t mask_len =
                        ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                        ((uint32_t)data[2] << 8) | ((uint32_t)data[3]);
                    data += 4;
                    remaining -= 4;

                    /* Validate mask_len is reasonable */
                    if (mask_len > 0 && mask_len <= remaining &&
                        data + mask_len <= data_end) {
                        data += mask_len;
                        remaining -= mask_len;
                    } else if (mask_len > remaining) {
                        /* Invalid mask length - abort parsing this layer's
                         * extra data */
                        goto skip_extra_parsing;
                    }
                }

                /* Skip layer blending ranges (starts with 4-byte length) */
                if (remaining >= 4) {
                    uint32_t blend_len =
                        ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                        ((uint32_t)data[2] << 8) | ((uint32_t)data[3]);
                    data += 4;
                    remaining -= 4;

                    /* Validate blend_len is reasonable */
                    if (blend_len > 0 && blend_len <= remaining &&
                        data + blend_len <= data_end) {
                        data += blend_len;
                        remaining -= blend_len;
                    } else if (blend_len > remaining) {
                        /* Invalid blend length - abort parsing this layer's
                         * extra data */
                        goto skip_extra_parsing;
                    }
                }

                /* Extract layer name (Pascal string, padded to multiple of 4)
                 */
                if (remaining >= 1) {
                    uint8_t name_len = data[0];
                    uint32_t name_total = 1 + name_len;

                    /* Pad to multiple of 4 */
                    if (name_total % 4u != 0u) {
                        name_total += 4u - (name_total % 4u);
                    }

                    if (name_total <= remaining && data + name_total <= data_end) {
                        if (layer->name == NULL && name_len > 0) {

                            const uint8_t *raw_name = &data[1];

                            /* Convert legacy MacRoman → UTF-8 */
                            size_t utf8_len = 0;
                            uint8_t *utf8 = psd_macroman_to_utf8(
                                doc->allocator,
                                raw_name,
                                name_len,
                                &utf8_len);

                            if (utf8) {
                                layer->name = utf8;            /* UTF-8, NUL-terminated */
                                layer->name_length = utf8_len; /* bytes excluding NUL */
                            }
                        }
                        data += name_total;
                        remaining -= name_total;
                    } else {
                        /* Can't read name - abort extra data parsing */
                        goto skip_extra_parsing;
                    }
                }

                /* Scan additional layer info blocks to detect features */
                while (remaining >=
                       12) { /* Minimum: signature (4) + key (4) + length (4) */
                    /* Ensure we don't read past the end */
                    if (data + 12 > data_end) {
                        break;
                    }

                    /* Signature at data[0-3], but we only care about the key */
                    uint32_t sig =
                        ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                        ((uint32_t)data[2] << 8) | ((uint32_t)data[3]);

                    /* Check for valid signature */
                    if (sig != 0x3842494D &&
                        sig != 0x38423634) { /* "8BIM" or "8B64" */
                        /* Not a valid block signature - stop parsing */
                        break;
                    }

                    uint8_t key[4] = {data[4], data[5], data[6], data[7]};
                    uint32_t block_len =
                        ((uint32_t)data[8] << 24) | ((uint32_t)data[9] << 16) |
                        ((uint32_t)data[10] << 8) | ((uint32_t)data[11]);

                    /* Validate block_len doesn't exceed remaining data */
                    if (block_len > remaining - 12) {
                        /* Block claims to be larger than remaining data */
                        break;
                    }

                    /* Detect features based on Additional Layer Information
                     * keys */
                    if (key[0] == 'T' && key[1] == 'y' && key[2] == 'S' &&
                        key[3] == 'h') {
                        /* TySh = Text layer */
                        layer->features.has_text = true;
                    } else if ((key[0] == 'S' && key[1] == 'o' &&
                                key[2] == 'L' && key[3] == 'd') ||
                               (key[0] == 'S' && key[1] == 'o' &&
                                key[2] == 'L' && key[3] == 'E')) {
                        /* SoLd/SoLE = Smart Object */
                        layer->features.has_smart_object = true;
                    } else if (key[0] == 'l' && key[1] == 'f' &&
                               key[2] == 'x' && key[3] == '2') {
                        /* lfx2 = Layer effects */
                        layer->features.has_effects = true;
                    } else if (key[0] == 'v' && key[1] == 'm' &&
                               key[2] == 's' && key[3] == 'k') {
                        /* vmsk = Vector mask */
                        layer->features.has_vector_mask = true;
                    } else if (key[0] == 'v' && key[1] == 'm' &&
                               key[2] == 'n' && key[3] == 's') {
                        /* vmns = Vector mask (alternate form) */
                        layer->features.has_vector_mask = true;
                    } else if (key[0] == 'a' && key[1] == 'd' &&
                               key[2] == 'j') {
                        /* adj* = Adjustment layer */
                        layer->features.has_adjustment = true;
                    } else if (
                        /* Common adjustment layer keys (non-exhaustive) */
                        (key[0] == 'b' && key[1] == 'r' && key[2] == 'i' && key[3] == 't') || /* Brightness/Contrast */
                        (key[0] == 'b' && key[1] == 'r' && key[2] == 't' && key[3] == 'C') || /* Brightness/Contrast (alt) */
                        (key[0] == 'l' && key[1] == 'e' && key[2] == 'v' && key[3] == 'l') || /* Levels */
                        (key[0] == 'c' && key[1] == 'u' && key[2] == 'r' && key[3] == 'v') || /* Curves */
                        (key[0] == 'h' && key[1] == 'u' && key[2] == 'e' && key[3] == ' ') || /* Hue/Saturation */
                        (key[0] == 'h' && key[1] == 'u' && key[2] == 'e' && key[3] == '2') || /* Hue/Saturation (v2) */
                        (key[0] == 'b' && key[1] == 'l' && key[2] == 'n' && key[3] == 'c') || /* Color Balance */
                        (key[0] == 'v' && key[1] == 'i' && key[2] == 'b' && key[3] == 'A') || /* Vibrance */
                        (key[0] == 'e' && key[1] == 'x' && key[2] == 'p' && key[3] == 'A') || /* Exposure */
                        (key[0] == 'm' && key[1] == 'i' && key[2] == 'x' && key[3] == 'r') || /* Channel Mixer */
                        (key[0] == 's' && key[1] == 'e' && key[2] == 'l' && key[3] == 'c') || /* Selective Color */
                        (key[0] == 't' && key[1] == 'h' && key[2] == 'r' && key[3] == 's') || /* Threshold */
                        (key[0] == 'p' && key[1] == 'o' && key[2] == 's' && key[3] == 't') || /* Posterize */
                        (key[0] == 'p' && key[1] == 'h' && key[2] == 'f' && key[3] == 'l') || /* Photo Filter */
                        (key[0] == 'g' && key[1] == 'r' && key[2] == 'd' && key[3] == 'm') || /* Gradient Map */
                        (key[0] == 'c' && key[1] == 'l' && key[2] == 'r' && key[3] == 'L')    /* Color Lookup */
                    ) {
                        layer->features.has_adjustment = true;
                    } else if (key[0] == '3' && key[1] == 'd' &&
                               key[2] == 'L') {
                        /* 3dL* = 3D layer */
                        layer->features.has_3d = true;
                    } else if (key[0] == 'l' && key[1] == 's' &&
                               key[2] == 'c' && key[3] == 't') {
                        /* lsct = Layer Section divider - group/folder marker */
                        if (block_len >= 4 &&
                            remaining >= 12 + block_len &&
                            data + 12 + block_len <= data_end) {

                            uint8_t *payload = data + 12;
                            /* Section type is FIRST field of lsct data */
                            uint32_t section_type = ((uint32_t)payload[0] << 24) |
                                                    ((uint32_t)payload[1] << 16) |
                                                    ((uint32_t)payload[2] << 8) |
                                                    ((uint32_t)payload[3]);

                            switch (section_type) {
                            case 1: /* Open folder */
                            case 2: /* Closed folder */
                                layer->features.is_group_start = true;
                                break;
                            case 3: /* Bounding section divider (group end) */
                                layer->features.is_group_end = true;
                                break;

                            default: /* Unknown section type */
                                break;
                            }
                        }
                    } else if ((key[0] == 'S' && key[1] == 'o' &&
                                key[2] == 'C' && key[3] == 'o') ||
                               (key[0] == 'G' && key[1] == 'd' &&
                                key[2] == 'F' && key[3] == 'l') ||
                               (key[0] == 'P' && key[1] == 't' &&
                                key[2] == 'F' && key[3] == 'l')) {
                        /* SoCo/GdFl/PtFl = Fill layer (Solid Color, Gradient,
                         * Pattern) */
                        layer->features.has_fill = true;
                    } else if (key[0] == 'v' && key[1] == 't' &&
                               key[2] == 'r' && key[3] == 'k') {
                        /* vtrk = Video layer */
                        layer->features.has_video = true;
                    } else if (key[0] == 'l' && key[1] == 'u' &&
                               key[2] == 'n' && key[3] == 'i') {
                        /* 'luni' = Unicode layer name */

                        if (block_len >= 4) {
                            const uint8_t *payload = data + 12;

                            uint32_t char_count =
                                ((uint32_t)payload[0] << 24) |
                                ((uint32_t)payload[1] << 16) |
                                ((uint32_t)payload[2] << 8) |
                                (uint32_t)payload[3];

                            size_t utf16_bytes = (size_t)char_count * 2;
                            if (4 + utf16_bytes <= block_len) {

                                size_t utf8_len = 0;
                                uint8_t *utf8 = psd_utf16be_to_utf8(
                                    doc->allocator,
                                    payload + 4,
                                    utf16_bytes,
                                    &utf8_len);

                                if (utf8) {
                                    /* Override legacy name */
                                    if (layer->name) {
                                        psd_alloc_free(doc->allocator, layer->name);
                                    }
                                    layer->name = utf8;
                                    layer->name_length = utf8_len;
                                }
                            }
                        }
                    }

                    /* Move to next block
                     * Adobe spec: signature(4) + key(4) + length(4) +
                     * data(length)
                     * Data length is padded to EVEN boundary (not 4-byte) */

                    /* Data length MUST be even (padded with 0 if odd) */
                    uint32_t padded_len = block_len;
                    if (block_len % 2 != 0) {
                        padded_len++;
                    }

                    uint32_t block_total = 12 + padded_len;

                    /* Ensure we don't go past the end */
                    if (block_total <= remaining &&
                        data + block_total <= data_end) {
                        data += block_total;
                        remaining -= block_total;
                    } else {
                        /* Can't advance safely */
                        break;
                    }
                }
            skip_extra_parsing
                :; /* Label for early exit from additional data parsing */
            }
        } else if (extra_length > 10000000) {
            /* Don't try to read unreasonably large extra data - skip it */
            /* This layer will be treated as empty/unsupported */
        }

        /* Check if we've gone past the section boundary - this indicates
         * misalignment */
        int64_t current_pos = psd_stream_tell(stream);
        if (current_pos < 0) {
            status = (psd_status_t)current_pos;
            goto error;
        }
        if (current_pos > layer_info_end) {
            /* We've read past the end of the layer section - severe
             * misalignment */
            status = PSD_ERR_CORRUPT_DATA;
            goto error;
        }
    }

    doc->layers.layer_count = layer_count;

    /* Channel image data section (compression type + pixel data for all layers)
     */
    /* This is stored AFTER all layer records, not as part of each layer record
     */
    /* Structure: For each layer, for each channel:
     *   - 2 bytes: compression type (0=RAW, 1=RLE, 2=ZIP, 3=ZIP+prediction)
     *   - Variable: pixel data (length was stored in channel descriptor)
     */

    int64_t channel_data_start = psd_stream_tell(stream);
    if (channel_data_start < 0) {
        return (psd_status_t)channel_data_start;
    }

    /* Determine whether per-channel lengths include the 2-byte compression field.
     *
     * Real-world writers differ; we can disambiguate by comparing the total
     * expected bytes in the channel image data section to the remaining bytes in
     * the Layer Info subsection.
     */
    int64_t remaining_channel_bytes = layer_info_end - channel_data_start;
    if (remaining_channel_bytes < 0) {
        status = PSD_ERR_CORRUPT_DATA;
        goto error;
    }

    uint64_t sum_channel_lengths = 0;
    uint64_t total_channels = 0;
    for (int32_t i = 0; i < layer_count; i++) {
        psd_layer_record_t *layer = &doc->layers.layers[i];
        total_channels += (uint64_t)layer->channel_count;
        for (uint16_t ch = 0; ch < layer->channel_count; ch++) {
            sum_channel_lengths += layer->channels[ch].compressed_length;
        }
    }

    const uint64_t rem_u64 = (uint64_t)remaining_channel_bytes;
    const bool lengths_exclude_compression =
        (sum_channel_lengths + 2u * total_channels == rem_u64);

    /* Parse channel image data for each layer */
    for (int32_t i = 0; i < layer_count; i++) {
        psd_layer_record_t *layer = &doc->layers.layers[i];

        /* Read channel image data for each channel */
        for (uint16_t ch = 0; ch < layer->channel_count; ch++) {
            psd_layer_channel_data_t *channel = &layer->channels[ch];

            /* Read compression type (2 bytes) */
            uint16_t compression = 0;
            status = psd_stream_read_be16(stream, &compression);
            if (status != PSD_OK) {
                goto error;
            }

            /* PSD channel compression must be 0..3 */
            if (compression > 3) {
                status = PSD_ERR_CORRUPT_DATA;
                goto error;
            }

            /* Store compression (keep full width if your struct supports it) */
            channel->compression = (uint8_t)compression;

            uint64_t data_length = channel->compressed_length;
            if (lengths_exclude_compression) {
                /* Length field is payload-only; keep as-is. */
            } else {
                /* Default/spec behavior: length includes the 2-byte compression field. */
                if (channel->compressed_length < 2) {
                    status = PSD_ERR_CORRUPT_DATA;
                    goto error;
                }
                data_length = channel->compressed_length - 2;
                /* Our in-memory compressed buffer stores ONLY the payload bytes (not the
                 * 2-byte compression field), so keep compressed_length consistent with
                 * compressed_data. */
                channel->compressed_length = data_length;
            }

            /* Allocate buffer for compressed data */
            channel->compressed_data = (uint8_t *)psd_alloc_malloc(doc->allocator, data_length);
            if (!channel->compressed_data) {
                status = PSD_ERR_OUT_OF_MEMORY;
                goto error;
            }

            /* Read compressed pixel data */
            int64_t read_bytes = psd_stream_read(stream, channel->compressed_data, data_length);
            if (read_bytes != (int64_t)data_length) {
                status = PSD_ERR_STREAM_EOF;
                goto error;
            }
        }
    }

    /* Validate layer info end */
    if (psd_stream_tell(stream) != layer_info_end) {
        psd_stream_seek(stream, layer_info_end);
    }

    /* ---- Global Layer Mask Info ---- */
    uint32_t global_mask_length;
    status = psd_stream_read_be32(stream, &global_mask_length);
    if (status != PSD_OK) {
        goto error;
    }

    if (global_mask_length > 0) {
        status = psd_stream_skip(stream, global_mask_length);
        if (status != PSD_OK) {
            goto error;
        }
    }

    int64_t pos = psd_stream_tell(stream);
    if (pos < 0) {
        return (psd_status_t)pos;
    }
    if (pos < layer_info_end) {
        /* Some PSDs pad layer info, skip forward */
        psd_stream_seek(stream, layer_info_end);
    }

    /* We are now somewhere after the Layer Info subsection and Global Layer Mask Info.
     * The PSD spec allows additional data inside the Layer and Mask Info section.
     * Ensure the stream is positioned at the end of the ENTIRE section so the
     * following Image Data (composite) section is aligned correctly. */
    int64_t final_pos = psd_stream_tell(stream);
    if (final_pos < 0) {
        return (psd_status_t)final_pos;
    }

    if (final_pos < section_end) {
        /* Skip any remaining (often additional layer info) bytes */
        int64_t seek_result = psd_stream_seek(stream, section_end);
        if (seek_result < 0) {
            return (psd_status_t)seek_result;
        }
    } else if (final_pos > section_end) {
        /* Overrun indicates corruption/misalignment */
        return PSD_ERR_CORRUPT_DATA;
    }

    return PSD_OK;

error:
    /* Free allocated layers on error */
    if (doc->layers.layers) {
        for (int32_t i = 0; i < layer_count; i++) {
            if (doc->layers.layers[i].channels) {
                psd_alloc_free(doc->allocator, doc->layers.layers[i].channels);
            }
            if (doc->layers.layers[i].additional_data) {
                psd_alloc_free(doc->allocator,
                               doc->layers.layers[i].additional_data);
            }
        }
        psd_alloc_free(doc->allocator, doc->layers.layers);
        doc->layers.layers = NULL;
    }
    return status;
}

/**
 * @brief Parse a PSD file
 */
PSD_API psd_document_t *psd_parse_ex(psd_stream_t *stream,
                                     const psd_allocator_t *allocator,
                                     psd_status_t *out_status) {
    if (out_status) {
        *out_status = PSD_OK;
    }
    if (!stream) {
        if (out_status) {
            *out_status = PSD_ERR_NULL_POINTER;
        }
        return NULL;
    }

    /* Allocate document structure */
    psd_document_t *doc =
        (psd_document_t *)psd_alloc_malloc(allocator, sizeof(*doc));
    if (!doc) {
        if (out_status) {
            *out_status = PSD_ERR_OUT_OF_MEMORY;
        }
        return NULL;
    }

    doc->allocator = allocator;
    doc->is_psb = false;
    doc->width = 0;
    doc->height = 0;
    doc->channels = 0;
    doc->depth = 0;
    doc->color_mode = (psd_color_mode_t)0;
    doc->color_data.data = NULL;
    doc->color_data.length = 0;
    doc->resources.blocks = NULL;
    doc->resources.count = 0;
    doc->layers.layers = NULL;
    doc->layers.layer_count = 0;
    doc->layers.has_transparency_layer = false;
    doc->composite.data = NULL;
    doc->composite.data_length = 0;
    doc->composite.compression = PSD_COMPRESSION_RAW;
    doc->text_layers.items = NULL;
    doc->text_layers.count = 0;

    /* Parse header */
    psd_status_t status = psd_parse_header(stream, doc);
    if (status != PSD_OK) {
        psd_alloc_free(allocator, doc);
        if (out_status) {
            *out_status = status;
        }
        return NULL;
    }

    /* Parse color mode data section */
    status = psd_parse_color_mode_data(stream, doc);
    if (status != PSD_OK) {
        psd_alloc_free(allocator, doc);
        if (out_status) {
            *out_status = status;
        }
        return NULL;
    }

    /* Parse image resources section */
    status = psd_parse_resources(stream, doc);
    if (status != PSD_OK) {
        /* Free color data on error */
        if (doc->color_data.data) {
            psd_alloc_free(allocator, doc->color_data.data);
        }
        psd_alloc_free(allocator, doc);
        if (out_status) {
            *out_status = status;
        }
        return NULL;
    }

    /* Parse layer and mask information section */
    status = psd_parse_layer_info(stream, doc);
    if (status != PSD_OK) {
        /* Free all previously allocated data on error */
        if (doc->color_data.data) {
            psd_alloc_free(allocator, doc->color_data.data);
        }
        if (doc->resources.blocks) {
            for (size_t i = 0; i < doc->resources.count; i++) {
                if (doc->resources.blocks[i].name) {
                    psd_alloc_free(allocator, doc->resources.blocks[i].name);
                }
                if (doc->resources.blocks[i].data) {
                    psd_alloc_free(allocator, doc->resources.blocks[i].data);
                }
            }
            psd_alloc_free(allocator, doc->resources.blocks);
        }
        psd_alloc_free(allocator, doc);
        if (out_status) {
            *out_status = status;
        }
        return NULL;
    }

    /* Parse text layers from additional layer info blocks */
    psd_status_t text_status = psd_parse_text_layers(doc);
    if (text_status != PSD_OK) {
        /* Log text parsing failure but continue parsing */
        /* Text parsing failure should not prevent PSD loading */
    }

    /* Parse composite image data section */
    status = psd_parse_composite_image(stream, doc);
    if (status != PSD_OK) {
        /* If we hit EOF, stream error, or unsupported compression, composite
         * data is optional - don't fail */
        if (status != PSD_ERR_STREAM_EOF && status != PSD_ERR_STREAM_INVALID &&
            status != PSD_ERR_UNSUPPORTED_COMPRESSION) {
            /* Real error - free and return */
            if (doc->color_data.data) {
                psd_alloc_free(allocator, doc->color_data.data);
            }
            if (doc->resources.blocks) {
                for (size_t i = 0; i < doc->resources.count; i++) {
                    if (doc->resources.blocks[i].name) {
                        psd_alloc_free(allocator,
                                       doc->resources.blocks[i].name);
                    }
                    if (doc->resources.blocks[i].data) {
                        psd_alloc_free(allocator,
                                       doc->resources.blocks[i].data);
                    }
                }
                psd_alloc_free(allocator, doc->resources.blocks);
            }
            psd_alloc_free(allocator, doc);
            if (out_status) {
                *out_status = status;
            }
            return NULL;
        }
        /* Otherwise, composite data is missing/optional - continue */
    }

    return doc;
}

PSD_API psd_document_t *psd_parse(psd_stream_t *stream,
                                  const psd_allocator_t *allocator) {
    return psd_parse_ex(stream, allocator, NULL);
}

/**
 * @brief Free a document
 */
PSD_API psd_status_t psd_document_free(psd_document_t *doc) {
    if (!doc) {
        return PSD_OK;
    }

    const psd_allocator_t *allocator = doc->allocator;

    /* Free color mode data if present */
    if (doc->color_data.data) {
        psd_alloc_free(allocator, doc->color_data.data);
        doc->color_data.data = NULL;
        doc->color_data.length = 0;
    }

    /* Free resource blocks */
    if (doc->resources.blocks) {
        for (size_t i = 0; i < doc->resources.count; i++) {
            if (doc->resources.blocks[i].name) {
                psd_alloc_free(allocator, doc->resources.blocks[i].name);
            }
            if (doc->resources.blocks[i].data) {
                psd_alloc_free(allocator, doc->resources.blocks[i].data);
            }
        }
        psd_alloc_free(allocator, doc->resources.blocks);
        doc->resources.blocks = NULL;
        doc->resources.count = 0;
    }

    /* Free layer information */
    if (doc->layers.layers) {
        for (int32_t i = 0; i < doc->layers.layer_count; i++) {
            /* Free channel data (both compressed and decoded) */
            if (doc->layers.layers[i].channels) {
                for (size_t j = 0; j < doc->layers.layers[i].channel_count;
                     j++) {
                    psd_layer_channel_data_t *channel =
                        &doc->layers.layers[i].channels[j];

                    /* Free compressed data if separate from decoded */
                    if (channel->compressed_data) {
                        psd_alloc_free(allocator, channel->compressed_data);
                    }

                    /* Free decoded data if it was allocated separately */
                    if (channel->decoded_data &&
                        channel->decoded_data != channel->compressed_data) {
                        psd_alloc_free(allocator, channel->decoded_data);
                    }
                }
                psd_alloc_free(allocator, doc->layers.layers[i].channels);
            }

            if (doc->layers.layers[i].name) {
                psd_alloc_free(allocator, doc->layers.layers[i].name);
            }
            if (doc->layers.layers[i].additional_data) {
                psd_alloc_free(allocator,
                               doc->layers.layers[i].additional_data);
            }
            /* Free descriptor if present */
            if (doc->layers.layers[i].descriptor) {
                psd_descriptor_free(doc->layers.layers[i].descriptor,
                                    allocator);
            }
        }
        psd_alloc_free(allocator, doc->layers.layers);
        doc->layers.layers = NULL;
        doc->layers.layer_count = 0;
    }

    /* Free composite image data */
    if (doc->composite.data) {
        psd_alloc_free(allocator, doc->composite.data);
        doc->composite.data = NULL;
        doc->composite.data_length = 0;
    }

    /* Free text layers derived database */
    psd_free_text_layers(doc);

    /* Free document structure */
    psd_alloc_free(allocator, doc);

    return PSD_OK;
}

/**
 * @brief Parse Composite Image Data section
 *
 * Reads the composite image, which is the final flattened rendering.
 * Supports RAW, RLE, ZIP, and ZIP with prediction compression.
 * For now, ZIP formats return unsupported error.
 *
 * @param stream Stream to read from
 * @param doc Document to populate
 * @return PSD_OK on success, negative error code on failure
 */
static psd_status_t psd_parse_composite_image(psd_stream_t *stream,
                                              psd_document_t *doc) {
    psd_status_t status;
    uint16_t compression;
    const psd_allocator_t *alloc = doc->allocator;
    int64_t counts_pos = 0;

    /* According to PSD spec, Image Data section format is:
     * 2 bytes: Compression method (0=raw, 1=RLE, 2=ZIP, 3=ZIP+prediction)
     * Variable: Image data in planar order (RRR GGG BBB...)
     *
     * Note: There is NO length field before the compression type in the Image
     * Data section!
     *
     * Special case: If there's no image data (e.g., for 1-bit or indexed
     * color), this section may be empty.
     */

    /* Try to read compression type */
    status = psd_stream_read_be16(stream, &compression);
    if (status != PSD_OK) {
        /* End of file or read error - no composite image */
        doc->composite.data = NULL;
        doc->composite.data_length = 0;
        doc->composite.compression = PSD_COMPRESSION_RAW;
        return PSD_OK;
    }

    if (compression > 3) {
        return PSD_ERR_UNSUPPORTED_COMPRESSION;
    }

    doc->composite.compression = (psd_compression_t)compression;

    /* Calculate expected uncompressed size for planar composite data.
     *
     * For depth 8/16/32:
     *   plane bytes = width * height * (depth/8)
     * For depth 1 (bitmap):
     *   each scanline is packed bits -> row bytes = (width + 7) / 8
     *   plane bytes = row_bytes * height
     */
    uint64_t bytes_per_sample = (doc->depth == 1) ? 1u : (uint64_t)(doc->depth / 8u);
    uint64_t bytes_per_scanline =
        (doc->depth == 1) ? (((uint64_t)doc->width + 7u) / 8u)
                          : ((uint64_t)doc->width * bytes_per_sample);
    uint64_t uncompressed_size =
        (uint64_t)doc->channels * (uint64_t)doc->height * bytes_per_scanline;

    /* Allocate buffer for image data */
    doc->composite.data = (uint8_t *)psd_alloc_malloc(alloc, uncompressed_size);
    if (!doc->composite.data) {
        return PSD_ERR_OUT_OF_MEMORY;
    }

    doc->composite.data_length = uncompressed_size;

    /* Handle different compression types */
    switch (compression) {
    case PSD_COMPRESSION_RAW: {
        /* RAW: data is uncompressed */
        int64_t read_bytes =
            psd_stream_read(stream, doc->composite.data, uncompressed_size);
        if ((uint64_t)read_bytes != uncompressed_size) {
            psd_alloc_free(alloc, doc->composite.data);
            doc->composite.data = NULL;
            return PSD_ERR_STREAM_EOF;
        }
        break;
    }

    case PSD_COMPRESSION_RLE: {
        /* RLE: PackBits compression per scanline */
        uint32_t num_scanlines = doc->height * doc->channels;
        counts_pos = psd_stream_tell(stream);
        if (counts_pos < 0) {
            psd_alloc_free(alloc, doc->composite.data);
            doc->composite.data = NULL;
            return PSD_ERR_STREAM_INVALID;
        }

        /* PSD commonly uses 2-byte counts; PSB commonly uses 4-byte counts. */
        psd_status_t st2 = PSD_ERR_CORRUPT_DATA;
        psd_status_t st4 = PSD_ERR_CORRUPT_DATA;
        if (doc->is_psb) {
            st4 = psd_try_decode_composite_rle(
                stream, counts_pos, num_scanlines, bytes_per_scanline,
                doc->composite.data, uncompressed_size, alloc, 4);
            if (st4 != PSD_OK) {
                st2 = psd_try_decode_composite_rle(
                    stream, counts_pos, num_scanlines, bytes_per_scanline,
                    doc->composite.data, uncompressed_size, alloc, 2);
            }
            status = (st4 == PSD_OK) ? PSD_OK : st2;
        } else {
            st2 = psd_try_decode_composite_rle(
                stream, counts_pos, num_scanlines, bytes_per_scanline,
                doc->composite.data, uncompressed_size, alloc, 2);
            if (st2 != PSD_OK) {
                st4 = psd_try_decode_composite_rle(
                    stream, counts_pos, num_scanlines, bytes_per_scanline,
                    doc->composite.data, uncompressed_size, alloc, 4);
            }
            status = (st2 == PSD_OK) ? PSD_OK : st4;
        }

        if (status != PSD_OK) {
            psd_alloc_free(alloc, doc->composite.data);
            doc->composite.data = NULL;
            return PSD_ERR_CORRUPT_DATA;
        }

        break;
    }

    case PSD_COMPRESSION_ZIP: {
        /* ZIP compression - allocate buffer for compressed data */
        uint8_t *compressed_data =
            (uint8_t *)psd_alloc_malloc(alloc, uncompressed_size * 2);
        if (!compressed_data) {
            psd_alloc_free(alloc, doc->composite.data);
            doc->composite.data = NULL;
            return PSD_ERR_OUT_OF_MEMORY;
        }

        int64_t read_bytes =
            psd_stream_read(stream, compressed_data, uncompressed_size * 2);
        if (read_bytes <= 0) {
            psd_alloc_free(alloc, compressed_data);
            psd_alloc_free(alloc, doc->composite.data);
            doc->composite.data = NULL;
            return PSD_ERR_STREAM_EOF;
        }

        psd_status_t zip_status =
            psd_zip_decompress(compressed_data, (size_t)read_bytes,
                               doc->composite.data, uncompressed_size, alloc);
        psd_alloc_free(alloc, compressed_data);

        if (zip_status != PSD_OK) {
            psd_alloc_free(alloc, doc->composite.data);
            doc->composite.data = NULL;
            return zip_status;
        }
        break;
    }

    case PSD_COMPRESSION_ZIP_PRED: {
        uint8_t *compressed_data =
            (uint8_t *)psd_alloc_malloc(alloc, uncompressed_size * 2);
        if (!compressed_data) {
            psd_alloc_free(alloc, doc->composite.data);
            doc->composite.data = NULL;
            return PSD_ERR_OUT_OF_MEMORY;
        }

        int64_t read_bytes =
            psd_stream_read(stream, compressed_data, uncompressed_size * 2);
        if (read_bytes <= 0) {
            psd_alloc_free(alloc, compressed_data);
            psd_alloc_free(alloc, doc->composite.data);
            doc->composite.data = NULL;
            return PSD_ERR_STREAM_EOF;
        }

        /* Composite data is planar, so prediction is applied per-channel scanlines. */
        uint64_t bytes_per_pixel = (doc->depth == 1) ? 1u : bytes_per_sample;
        uint64_t scanline_width = bytes_per_scanline;
        psd_status_t zip_status = psd_zip_decompress_with_prediction(
            compressed_data, (size_t)read_bytes, doc->composite.data,
            uncompressed_size, (size_t)scanline_width, (size_t)bytes_per_pixel, alloc);

        psd_alloc_free(alloc, compressed_data);

        if (zip_status != PSD_OK) {
            psd_alloc_free(alloc, doc->composite.data);
            doc->composite.data = NULL;
            return zip_status;
        }
        break;
    }

    default:
        psd_alloc_free(alloc, doc->composite.data);
        doc->composite.data = NULL;
        return PSD_ERR_UNSUPPORTED_COMPRESSION;
    }

    return PSD_OK;
}

/**
 * @brief Get document dimensions
 */
PSD_API psd_status_t psd_document_get_dimensions(const psd_document_t *doc,
                                                 uint32_t *width,
                                                 uint32_t *height) {
    if (!doc) {
        return PSD_ERR_NULL_POINTER;
    }

    if (width) {
        *width = doc->width;
    }
    if (height) {
        *height = doc->height;
    }

    return PSD_OK;
}

/**
 * @brief Get document color mode
 */
PSD_API psd_status_t psd_document_get_color_mode(const psd_document_t *doc,
                                                 psd_color_mode_t *color_mode) {
    if (!doc || !color_mode) {
        return PSD_ERR_NULL_POINTER;
    }

    *color_mode = doc->color_mode;
    return PSD_OK;
}

/**
 * @brief Get document bit depth
 */
PSD_API psd_status_t psd_document_get_depth(const psd_document_t *doc,
                                            uint16_t *depth) {
    if (!doc || !depth) {
        return PSD_ERR_NULL_POINTER;
    }

    *depth = doc->depth;
    return PSD_OK;
}

/**
 * @brief Get number of color channels
 */
PSD_API psd_status_t psd_document_get_channels(const psd_document_t *doc,
                                               uint16_t *channels) {
    if (!doc || !channels) {
        return PSD_ERR_NULL_POINTER;
    }

    *channels = doc->channels;
    return PSD_OK;
}

/**
 * @brief Check if document is PSB format
 */
PSD_API psd_status_t psd_document_is_psb(const psd_document_t *doc,
                                         bool *is_psb) {
    if (!doc || !is_psb) {
        return PSD_ERR_NULL_POINTER;
    }

    *is_psb = doc->is_psb;
    return PSD_OK;
}

/**
 * @brief Get raw color mode data
 */
PSD_API psd_status_t psd_document_get_color_mode_data(const psd_document_t *doc,
                                                      const uint8_t **data,
                                                      uint64_t *length) {
    if (!doc) {
        return PSD_ERR_NULL_POINTER;
    }

    if (data) {
        *data = doc->color_data.data;
    }
    if (length) {
        *length = doc->color_data.length;
    }

    return PSD_OK;
}

/**
 * @brief Get number of image resources
 */
PSD_API psd_status_t psd_document_get_resource_count(const psd_document_t *doc,
                                                     size_t *count) {
    if (!doc || !count) {
        return PSD_ERR_NULL_POINTER;
    }

    *count = doc->resources.count;
    return PSD_OK;
}

/**
 * @brief Get information about a resource block
 */
PSD_API psd_status_t psd_document_get_resource(const psd_document_t *doc,
                                               size_t index, uint16_t *id,
                                               const uint8_t **data,
                                               uint64_t *length) {
    if (!doc || !id) {
        return PSD_ERR_NULL_POINTER;
    }

    if (index >= doc->resources.count) {
        return PSD_ERR_OUT_OF_RANGE;
    }

    const psd_resource_block_t *block = &doc->resources.blocks[index];

    *id = block->id;
    if (data) {
        *data = block->data;
    }
    if (length) {
        *length = block->data_length;
    }

    return PSD_OK;
}

/**
 * @brief Find a resource by ID
 */
PSD_API psd_status_t psd_document_find_resource(const psd_document_t *doc,
                                                uint16_t id, size_t *index) {
    if (!doc || !index) {
        return PSD_ERR_NULL_POINTER;
    }

    for (size_t i = 0; i < doc->resources.count; i++) {
        if (doc->resources.blocks[i].id == id) {
            *index = i;
            return PSD_OK;
        }
    }

    return PSD_ERR_INVALID_ARGUMENT; /* Not found */
}

/**
 * @brief Get number of layers
 */
PSD_API psd_status_t psd_document_get_layer_count(const psd_document_t *doc,
                                                  int32_t *count) {
    if (!doc || !count) {
        return PSD_ERR_NULL_POINTER;
    }

    *count = doc->layers.layer_count;
    return PSD_OK;
}

/**
 * @brief Check for transparency layer
 */
PSD_API psd_status_t psd_document_has_transparency_layer(
    const psd_document_t *doc, bool *has_transparency) {
    if (!doc || !has_transparency) {
        return PSD_ERR_NULL_POINTER;
    }

    *has_transparency = doc->layers.has_transparency_layer;
    return PSD_OK;
}

/**
 * @brief Get layer bounds
 */
PSD_API psd_status_t psd_document_get_layer_bounds(const psd_document_t *doc,
                                                   int32_t index, int32_t *top,
                                                   int32_t *left,
                                                   int32_t *bottom,
                                                   int32_t *right) {
    if (!doc) {
        return PSD_ERR_NULL_POINTER;
    }

    if (index < 0 || index >= doc->layers.layer_count) {
        return PSD_ERR_OUT_OF_RANGE;
    }

    const psd_layer_record_t *layer = &doc->layers.layers[index];

    if (top)
        *top = layer->bounds.top;
    if (left)
        *left = layer->bounds.left;
    if (bottom)
        *bottom = layer->bounds.bottom;
    if (right)
        *right = layer->bounds.right;

    return PSD_OK;
}

/**
 * @brief Get layer blend mode
 */
PSD_API psd_status_t
psd_document_get_layer_blend_mode(const psd_document_t *doc, int32_t index,
                                  uint32_t *blend_sig, uint32_t *blend_key) {
    if (!doc || !blend_sig || !blend_key) {
        return PSD_ERR_NULL_POINTER;
    }

    if (index < 0 || index >= doc->layers.layer_count) {
        return PSD_ERR_OUT_OF_RANGE;
    }

    const psd_layer_record_t *layer = &doc->layers.layers[index];

    *blend_sig = layer->blend_sig;
    *blend_key = layer->blend_key;

    return PSD_OK;
}

/**
 * @brief Get layer opacity and flags
 */
PSD_API psd_status_t
psd_document_get_layer_properties(const psd_document_t *doc, int32_t index,
                                  uint8_t *opacity, uint8_t *flags) {
    if (!doc) {
        return PSD_ERR_NULL_POINTER;
    }

    if (index < 0 || index >= doc->layers.layer_count) {
        return PSD_ERR_OUT_OF_RANGE;
    }

    const psd_layer_record_t *layer = &doc->layers.layers[index];

    if (opacity)
        *opacity = layer->opacity;
    if (flags)
        *flags = layer->flags;

    return PSD_OK;
}

/**
 * @brief Get layer channel count
 */
PSD_API psd_status_t psd_document_get_layer_channel_count(
    const psd_document_t *doc, int32_t layer_index, size_t *count) {
    if (!doc || !count) {
        return PSD_ERR_NULL_POINTER;
    }

    if (layer_index < 0 || layer_index >= doc->layers.layer_count) {
        return PSD_ERR_OUT_OF_RANGE;
    }

    const psd_layer_record_t *layer = &doc->layers.layers[layer_index];
    *count = layer->channel_count;

    return PSD_OK;
}

/**
 * @brief Get layer name
 *
 * Returns the layer name as a UTF-8 string.
 */
PSD_API psd_status_t psd_document_get_layer_name(const psd_document_t *doc,
                                                 int32_t layer_index,
                                                 const uint8_t **name,
                                                 size_t *name_length) {
    if (!doc) return PSD_ERR_NULL_POINTER;

    if (layer_index < 0 || layer_index >= doc->layers.layer_count) {
        return PSD_ERR_OUT_OF_RANGE;
    }

    psd_layer_record_t *layer = (psd_layer_record_t *)&doc->layers.layers[layer_index];

    if (!name || !name_length)  return PSD_ERR_NULL_POINTER;

    if (layer->name) {
        *name = layer->name;
        *name_length = layer->name_length;
        return PSD_OK;
    }
    
    return PSD_ERR_INVALID_ARGUMENT;
}

/**
 * @brief Get layer features
 *
 * Returns the features detected from the layer's Additional Layer Information.
 */
PSD_API psd_status_t
psd_document_get_layer_features(const psd_document_t *doc, int32_t layer_index,
                                psd_layer_features_t *features) {
    if (!doc || !features) {
        return PSD_ERR_NULL_POINTER;
    }

    if (layer_index < 0 || layer_index >= doc->layers.layer_count) {
        return PSD_ERR_OUT_OF_RANGE;
    }

    const psd_layer_record_t *layer = &doc->layers.layers[layer_index];
    *features = layer->features;

    return PSD_OK;
}

/**
 * @brief Get layer type
 *
 * Returns the layer type as an enum value, determined from the layer's features.
 */
PSD_API psd_status_t psd_document_get_layer_type(
    const psd_document_t *doc, int32_t layer_index, psd_layer_type_t *type) {
    if (!doc || !type) {
        return PSD_ERR_NULL_POINTER;
    }

    if (layer_index < 0 || layer_index >= doc->layers.layer_count) {
        return PSD_ERR_OUT_OF_RANGE;
    }

    const psd_layer_record_t *layer = &doc->layers.layers[layer_index];
    const psd_layer_features_t *features = &layer->features;

    /* Group end markers are structural only */
    if (features->is_group_start) {
        *type = PSD_LAYER_TYPE_GROUP_START;
    } else if (features->is_group_end) {
        *type = PSD_LAYER_TYPE_GROUP_END;
    } else if (features->has_text) {
        *type = PSD_LAYER_TYPE_TEXT;
    } else if (features->has_smart_object) {
        *type = PSD_LAYER_TYPE_SMART_OBJECT;
    } else if (features->has_adjustment) {
        *type = PSD_LAYER_TYPE_ADJUSTMENT;
    } else if (features->has_fill) {
        *type = PSD_LAYER_TYPE_FILL;
    } else if (features->has_effects) {
        *type = PSD_LAYER_TYPE_EFFECTS;
    } else if (features->has_3d) {
        *type = PSD_LAYER_TYPE_3D;
    } else if (features->has_video) {
        *type = PSD_LAYER_TYPE_VIDEO;
    } else if (layer->channel_count > 0) {
        *type = PSD_LAYER_TYPE_PIXEL;
    } else {
        *type = PSD_LAYER_TYPE_EMPTY; // valid but no pixels
    }

    return PSD_OK;
}

/**
 * @brief Check if a layer is a Photoshop Background layer
 *
 * A true background layer must meet ALL criteria:
 * 1. Bottom-most layer (index == layer_count - 1)
 * 2. Background flag set (bit 2 in flags byte)
 * 3. No transparency channel (channel ID -1)
 * 4. No layer mask data
 * 5. No vector mask (vmsk/vmns)
 * 6. Channel count equals base_channel_count
 */
PSD_API psd_bool_t psd_document_is_background_layer(const psd_document_t *doc,
                                                    int32_t layer_index,
                                                    int base_channel_count) {
    if (!doc) {
        return 0; /* false */
    }

    /* Criterion 1: Must be the bottom-most layer */
    if (layer_index != doc->layers.layer_count - 1) {
        return 0; /* false */
    }

    const psd_layer_record_t *layer = &doc->layers.layers[layer_index];

    /* Criterion 2: Check background flag (bit 2 in flags byte) */
    /* Bit 0 = transparency protected
       Bit 1 = visible
       Bit 2 = obsolete (but we use it to identify background)
       Bit 3 = 1 for PS 5.0+
       Bit 4 = pixel data irrelevant */
    if (!(layer->flags & 0x04)) { /* Bit 2 is not set */
        return 0;                 /* false */
    }

    /* Criterion 3: Check for transparency channel (channel_id == -1) */
    for (size_t i = 0; i < layer->channel_count; i++) {
        if (layer->channels[i].channel_id == -1) {
            return 0; /* Has transparency, so not a true background */
        }
    }

    /* Criterion 4: Check for layer mask data
       Layer mask data is the first field in additional_data, with a 4-byte
       length prefix. If mask_len > 0, a layer mask exists. */
    if (layer->additional_data && layer->additional_length > 0) {
        if (layer->additional_length >= 4) {
            uint32_t mask_len = ((uint32_t)layer->additional_data[0] << 24) |
                                ((uint32_t)layer->additional_data[1] << 16) |
                                ((uint32_t)layer->additional_data[2] << 8) |
                                ((uint32_t)layer->additional_data[3]);
            if (mask_len > 0) {
                return 0; /* Has layer mask, not a true background */
            }
        }
    }

    /* Criterion 5: Check for vector mask (vmsk/vmns keys) in additional layer
       info blocks Structure of additional_data:
       - Layer mask data (4-byte length + data)
       - Layer blending ranges (4-byte length + data)
       - Layer name (Pascal string, padded to 4)
       - Additional layer info blocks (tagged blocks with 4-byte key + 4-byte
       length + data)
    */
    if (layer->additional_data && layer->additional_length > 0) {
        uint8_t *data = layer->additional_data;
        uint64_t remaining = layer->additional_length;

        /* Skip layer mask data */
        if (remaining >= 4) {
            uint32_t mask_len = ((uint32_t)data[0] << 24) |
                                ((uint32_t)data[1] << 16) |
                                ((uint32_t)data[2] << 8) | ((uint32_t)data[3]);
            data += 4;
            remaining -= 4;
            if (mask_len > 0 && mask_len <= remaining) {
                data += mask_len;
                remaining -= mask_len;
            }
        }

        /* Skip layer blending ranges */
        if (remaining >= 4) {
            uint32_t blend_len = ((uint32_t)data[0] << 24) |
                                 ((uint32_t)data[1] << 16) |
                                 ((uint32_t)data[2] << 8) | ((uint32_t)data[3]);
            data += 4;
            remaining -= 4;
            if (blend_len > 0 && blend_len <= remaining) {
                data += blend_len;
                remaining -= blend_len;
            }
        }

        /* Skip layer name (Pascal string, padded to 4) */
        if (remaining >= 1) {
            uint8_t name_len = data[0];
            uint32_t name_total = 1 + name_len;
            if (name_total % 4 != 0) {
                name_total += 4 - (name_total % 4);
            }
            if (name_total <= remaining) {
                data += name_total;
                remaining -= name_total;
            }
        }

        /* Now scan additional layer info blocks for vector mask (vmsk/vmns) */
        while (remaining >= 8) { /* 4-byte key + 4-byte length minimum */
            uint8_t *key = data;
            uint32_t block_len = ((uint32_t)data[4] << 24) |
                                 ((uint32_t)data[5] << 16) |
                                 ((uint32_t)data[6] << 8) | ((uint32_t)data[7]);

            /* Check for vector mask keys (vmsk = vector mask, vmns = vector
             * mask setting2) */
            if ((key[0] == 'v' && key[1] == 'm' && key[2] == 's' &&
                 key[3] == 'k') ||
                (key[0] == 'v' && key[1] == 'm' && key[2] == 'n' &&
                 key[3] == 's')) {
                return 0; /* Has vector mask, not a true background */
            }

            /* Move to next block */
            uint64_t block_total = 8 + block_len;
            if (block_total > remaining) {
                break;
            }
            data += block_total;
            remaining -= block_total;
        }
    }

    /* Criterion 6: Channel count must equal base_channel_count
       Background layers must have exactly the base color channels, no alpha, no
       masks */
    if ((int)layer->channel_count != base_channel_count) {
        return 0; /* Wrong number of channels */
    }

    /* All criteria met - this is a true background layer */
    return 1; /* true */
}

/**
 * @brief Get composite image data
 */
PSD_API psd_status_t psd_document_get_composite_image(const psd_document_t *doc,
                                                      const uint8_t **data,
                                                      uint64_t *length,
                                                      uint32_t *compression) {
    if (!doc) {
        return PSD_ERR_NULL_POINTER;
    }

    if (data) {
        *data = doc->composite.data;
    }
    if (length) {
        *length = doc->composite.data_length;
    }
    if (compression) {
        *compression = (uint32_t)doc->composite.compression;
    }

    return PSD_OK;
}

/**
 * @brief Get layer channel data with lazy decoding
 */
PSD_API psd_status_t psd_document_get_layer_channel_data(
    psd_document_t *doc, int32_t layer_index, size_t channel_index,
    int16_t *channel_id, const uint8_t **data, uint64_t *length,
    uint32_t *compression) {
    if (!doc) {
        return PSD_ERR_NULL_POINTER;
    }

    if (channel_id)
        *channel_id = 0;
    if (data)
        *data = NULL;
    if (length)
        *length = 0;
    if (compression)
        *compression = 0;

    if (layer_index < 0 || layer_index >= doc->layers.layer_count) {
        return PSD_ERR_OUT_OF_RANGE;
    }

    psd_layer_record_t *layer = &doc->layers.layers[layer_index];

    if (channel_index >= layer->channel_count) {
        return PSD_ERR_OUT_OF_RANGE;
    }

    psd_layer_channel_data_t *channel = &layer->channels[channel_index];

    /* Calculate layer dimensions from bounds */
    uint32_t layer_width = (uint32_t)(layer->bounds.right - layer->bounds.left);
    uint32_t layer_height = (uint32_t)(layer->bounds.bottom - layer->bounds.top);

    /* Ensure dimensions are valid (non-zero) */
    if (layer_width == 0 || layer_height == 0) {
        /* Empty layer - no data to decode */
        if (data) {
            *data = NULL;
        }
        if (length) {
            *length = 0;
        }
        return PSD_OK;
    }

    /* Lazy decode the channel if not already decoded */
    if (!channel->is_decoded) {

        uint16_t channel_depth;
        if (channel->channel_id == -2) {
            channel_depth = 8;
        } else if (channel->channel_id == -3) {
            channel_depth = 8;
        } else {
            channel_depth = doc->depth;
        }

        /* Decode all formats (RAW, RLE, ZIP, ZIP+prediction) */
        psd_status_t status = psd_layer_channel_decode(
            channel, layer_width, layer_height, channel_depth, doc->allocator);

        if (status != PSD_OK && status != PSD_ERR_UNSUPPORTED_COMPRESSION) {
            return status;
        }
    }

    /* Return information to caller */
    if (channel_id) {
        *channel_id = channel->channel_id;
    }

    /* Check if channel data is available */
    if (!channel->decoded_data && !channel->compressed_data) {
        /* Channel data was never loaded from the file */
        if (data) {
            *data = NULL;
        }
        if (length) {
            *length = 0;
        }
        return PSD_ERR_CORRUPT_DATA;
    }

    /* Return decoded data if available, otherwise compressed data */
    if (data) {
        *data = channel->decoded_data ? channel->decoded_data
                                      : channel->compressed_data;
    }

    if (length) {
        *length = channel->decoded_data ? channel->decoded_length
                                        : channel->compressed_length;
    }

    if (compression) {
        *compression = channel->compression;
    }

    if (channel->compression > 3) {
        return PSD_ERR_CORRUPT_DATA; /* or PSD_ERR_INTERNAL */
    }

    return PSD_OK;
}

/**
 * @brief Get layer descriptor data
 */
PSD_API psd_status_t psd_document_get_layer_descriptor(
    const psd_document_t *doc, int32_t layer_index,
    const uint8_t **descriptor_data, uint64_t *descriptor_length) {
    if (!doc) {
        return PSD_ERR_NULL_POINTER;
    }

    if (layer_index < 0 || layer_index >= doc->layers.layer_count) {
        return PSD_ERR_OUT_OF_RANGE;
    }

    const psd_layer_record_t *layer = &doc->layers.layers[layer_index];

    if (descriptor_data) {
        /* Return raw descriptor data if present */
        if (layer->descriptor) {
            /* For now, we don't expose the parsed descriptor structure,
             * only the raw additional data which may contain it */
            *descriptor_data = NULL;
        } else {
            *descriptor_data = NULL;
        }
    }

    if (descriptor_length) {
        *descriptor_length = 0;
    }

    return PSD_OK;
}
