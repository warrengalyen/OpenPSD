/**
 * @file psd_text_layer_parse.c
 * @brief Text layer parsing implementation
 *
 * Parses TySh (Photoshop 6+) and tySh (legacy) additional layer info blocks.
 * Only extracts essential rendering data upfront; full metadata parsing deferred to public API.
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

#include "psd_text_layer_parse.h"
#include "psd_text_layer.h"
#include "psd_descriptor.h"
#include "psd_layer.h"
#include "psd_context.h"
#include "psd_alloc.h"
#include "psd_endian.h"
#include "../include/openpsd/psd_stream.h"
#include <string.h>
#include <stdint.h>

/**
 * @brief Read big-endian IEEE754 double from stream
 */
static psd_status_t psd_stream_read_be_double(psd_stream_t *stream, double *val)
{
    uint64_t bits;
    psd_status_t status = psd_stream_read_be64(stream, &bits);
    if (status != PSD_OK) {
        return status;
    }
    union {
        uint64_t u;
        double d;
    } u;
    u.u = bits;
    *val = u.d;
    return PSD_OK;
}

/**
 * @brief Parse 'TySh' (Photoshop 6+) payload
 *
 * OPTIMIZATION: Only extracts rendering-essential data:
 * - Transform matrix (positioning/rotation)
 * - Text bounds (size)
 * 
 * Full descriptor parsing is deferred to on-demand public API functions.
 * Raw payload is always stored for later detailed parsing if needed.
 */
static psd_status_t psd_parse_tysh_payload(
    psd_document_t *doc,
    uint32_t layer_index,
    const psd_layer_record_t *layer,
    const uint8_t *payload,
    uint64_t payload_len,
    psd_text_layer_t *out_item)
{
    psd_stream_t *stream = psd_stream_create_buffer(doc->allocator, payload, (size_t)payload_len);
    if (!stream) {
        return PSD_ERR_OUT_OF_MEMORY;
    }

    memset(out_item, 0, sizeof(*out_item));
    out_item->layer_index = layer_index;
    out_item->source = PSD_TEXT_SOURCE_TYSH;
    out_item->text_data = NULL;
    out_item->warp_data = NULL;

    /* 1. TySh version (uint16) */
    psd_stream_read_be16(stream, &out_item->tysh_version);

    /* 2. Transform: 6 doubles - ESSENTIAL for rendering */
    psd_stream_read_be_double(stream, &out_item->transform.xx);
    psd_stream_read_be_double(stream, &out_item->transform.xy);
    psd_stream_read_be_double(stream, &out_item->transform.yx);
    psd_stream_read_be_double(stream, &out_item->transform.yy);
    psd_stream_read_be_double(stream, &out_item->transform.tx);
    psd_stream_read_be_double(stream, &out_item->transform.ty);

    /* 3. Text version (uint16) */
    psd_stream_read_be16(stream, &out_item->text_version);

    /* 4. Text descriptor version (uint32) - store version but skip descriptor */
    psd_stream_read_be32(stream, &out_item->text_desc_version);

    /* Skip to text bounds at end (they are 32 bytes from EOF: 4 doubles).
       We don't parse the complex descriptor structures for performance. */
    if (payload_len >= 70) {
        int64_t current = psd_stream_tell(stream);
        if (current >= 0) {
            int64_t bounds_start = (int64_t)payload_len - 32;
            if (bounds_start > current) {
                psd_stream_skip(stream, (size_t)(bounds_start - current));
            }
        }
    }

    /* Parse text bounds - ESSENTIAL for rendering: 4 doubles */
    psd_stream_read_be_double(stream, &out_item->text_bounds.left);
    psd_stream_read_be_double(stream, &out_item->text_bounds.top);
    psd_stream_read_be_double(stream, &out_item->text_bounds.right);
    psd_stream_read_be_double(stream, &out_item->text_bounds.bottom);

    /* Always store raw payload for on-demand parsing via public API */
    out_item->raw_tysh = (uint8_t *)psd_alloc_malloc(doc->allocator, payload_len);
    if (out_item->raw_tysh) {
        memcpy(out_item->raw_tysh, payload, (size_t)payload_len);
        out_item->raw_tysh_len = payload_len;
    }

    /* Determine if layer has rendered pixels */
    out_item->has_rendered_pixels = (layer->channel_count > 0) &&
                                    ((layer->bounds.right - layer->bounds.left) > 0) &&
                                    ((layer->bounds.bottom - layer->bounds.top) > 0);

    psd_stream_destroy(stream);
    return PSD_OK;
}

/**
 * @brief Helper to add text layer to derived database
 */
static psd_status_t psd_text_layers_add(
    psd_document_t *doc,
    const psd_text_layer_t *src)
{
    psd_text_layer_info_t *info = &doc->text_layers;
    const psd_allocator_t *allocator = doc->allocator;

    size_t new_count = info->count + 1;
    psd_text_layer_t *new_items;

    if (info->items == NULL) {
        new_items = (psd_text_layer_t *)psd_alloc_malloc(
            allocator,
            new_count * sizeof(psd_text_layer_t));
    } else {
        if (allocator && allocator->realloc) {
            new_items = (psd_text_layer_t *)allocator->realloc(
                info->items,
                new_count * sizeof(psd_text_layer_t),
                allocator->user_data);
        } else {
            new_items = (psd_text_layer_t *)psd_alloc_malloc(
                allocator,
                new_count * sizeof(psd_text_layer_t));
            if (new_items) {
                memcpy(new_items, info->items,
                       info->count * sizeof(psd_text_layer_t));
                psd_alloc_free(allocator, info->items);
            }
        }
    }

    if (!new_items) {
        return PSD_ERR_OUT_OF_MEMORY;
    }

    memcpy(&new_items[info->count], src, sizeof(psd_text_layer_t));
    info->items = new_items;
    info->count = new_count;

    return PSD_OK;
}

/**
 * @brief Parse all text layers from additional layer info blocks
 */
PSD_INTERNAL psd_status_t psd_parse_text_layers(psd_document_t *doc)
{
    if (!doc || doc->layers.layer_count <= 0) {
        return PSD_OK;
    }

    psd_free_text_layers(doc);

    for (int32_t i = 0; i < doc->layers.layer_count; i++) {
        psd_layer_record_t *layer = &doc->layers.layers[i];

        /* Only process layers with text feature */
        if (!layer->features.has_text || !layer->additional_data || layer->additional_length < 12) {
            continue;
        }

        const uint8_t *data = layer->additional_data;
        uint64_t remaining = layer->additional_length;

        /* Skip layer mask data (4-byte length + data) */
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

        /* Skip layer blending ranges (4-byte length + data) */
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

        /* Now scan additional layer info blocks */
        while (remaining >= 8) {
            const uint8_t *sig = data;
            const uint8_t *key = data + 4;
            uint32_t block_len = psd_read_be32(data + 8);

            uint64_t block_total = 12 + block_len;
            if (block_len > (remaining - 12)) {
                break;
            }

            if (block_len & 1) {
                block_total += 1;
            }

            if (block_total > remaining) {
                break;
            }

            uint32_t sig_val = psd_read_be32(sig);
            if (sig_val != 0x3842494D && sig_val != 0x3842364D) {
                data += block_total;
                remaining -= block_total;
                continue;
            }

            uint32_t key_val = psd_read_be32(key);
            const uint8_t *payload = data + 12;

            /* Parse TySh (Photoshop 6+) */
            if (key_val == 0x54795368) {
                psd_text_layer_t item;
                psd_status_t status = psd_parse_tysh_payload(
                    doc,
                    (uint32_t)i,
                    layer,
                    payload,
                    (uint64_t)block_len,
                    &item);

                if (status == PSD_OK) {
                    psd_text_layers_add(doc, &item);
                } else {
                    /* add with raw payload even on parse error */
                    item.layer_index = (uint32_t)i;
                    item.source = PSD_TEXT_SOURCE_TYSH;
                    item.raw_tysh = (uint8_t *)psd_alloc_malloc(doc->allocator, block_len);
                    if (item.raw_tysh) {
                        memcpy(item.raw_tysh, payload, (size_t)block_len);
                        item.raw_tysh_len = block_len;
                    }
                    item.has_rendered_pixels = (layer->channel_count > 0) &&
                                               ((layer->bounds.right - layer->bounds.left) > 0) &&
                                               ((layer->bounds.bottom - layer->bounds.top) > 0);
                    psd_text_layers_add(doc, &item);
                }
            }
            /* Parse tySh (legacy Photoshop 5/5.5) */
            else if (key_val == 0x74795368) {
                psd_text_layer_t item;
                memset(&item, 0, sizeof(item));
                item.layer_index = (uint32_t)i;
                item.source = PSD_TEXT_SOURCE_TYSH_LEGACY;

                item.raw_tysh = (uint8_t *)psd_alloc_malloc(doc->allocator, block_len);
                if (item.raw_tysh) {
                    memcpy(item.raw_tysh, payload, (size_t)block_len);
                    item.raw_tysh_len = block_len;
                }

                item.has_rendered_pixels = (layer->channel_count > 0) &&
                                           ((layer->bounds.right - layer->bounds.left) > 0) &&
                                           ((layer->bounds.bottom - layer->bounds.top) > 0);

                psd_text_layers_add(doc, &item);
            }

            data += block_total;
            remaining -= block_total;
        }
    }

    return PSD_OK;
}

/**
 * @brief Free all text layer data
 */
PSD_INTERNAL void psd_free_text_layers(psd_document_t *doc)
{
    if (!doc || !doc->text_layers.items) {
        return;
    }

    const psd_allocator_t *allocator = doc->allocator;

    for (size_t i = 0; i < doc->text_layers.count; i++) {
        psd_text_layer_t *item = &doc->text_layers.items[i];

        if (item->raw_tysh) {
            psd_alloc_free(allocator, item->raw_tysh);
            item->raw_tysh = NULL;
        }
        if (item->raw_text_engine) {
            psd_alloc_free(allocator, item->raw_text_engine);
            item->raw_text_engine = NULL;
        }

        if (item->text_data) {
            psd_descriptor_free(item->text_data, allocator);
            item->text_data = NULL;
        }
        if (item->warp_data) {
            psd_descriptor_free(item->warp_data, allocator);
            item->warp_data = NULL;
        }
    }

    psd_alloc_free(allocator, doc->text_layers.items);
    doc->text_layers.items = NULL;
    doc->text_layers.count = 0;
}
