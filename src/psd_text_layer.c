/**
 * @file psd_text_layer.c
 * @brief Text layer public API and utilities
 *
 * Implements public accessors for text layer information.
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
#include "psd_text_layer.h"
#include "psd_context.h"
#include "psd_descriptor.h"
#include "psd_alloc.h"
#include "psd_endian.h"
#include "psd_unicode.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* Enable noisy debug for text descriptor parsing by defining PSD_TEXT_LAYER_DEBUG */
#ifdef OPENPSD_TEXT_LAYER_DEBUG
#define PSD_TL_DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define PSD_TL_DEBUG(...) ((void)0)
#endif

/**
 * @brief Lazy-parse text and warp descriptors from raw TySh payload
 *
 * Parses descriptors only once, on first access. Subsequent calls return cached result.
 * This keeps initial PSD parsing fast while enabling descriptor access on demand.
 *
 * @param doc Document allocator and context
 * @param item Text layer to parse descriptors for (modified in-place)
 * @return PSD_OK on success, error code on failure
 */
static psd_status_t psd_text_layer_ensure_descriptors_parsed(
    psd_document_t *doc,
    psd_text_layer_t *item)
{
    if (!doc || !item) {
        return PSD_ERR_NULL_POINTER;
    }

    /* Already parsed? Return success */
    if (item->text_data != NULL) {
        return PSD_OK;
    }

    /* No raw data available? Can't parse */
    if (!item->raw_tysh || item->raw_tysh_len == 0) {
        return PSD_ERR_CORRUPT_DATA;
    }

    PSD_TL_DEBUG("DEBUG: Lazy-parsing descriptors for layer %u\n", item->layer_index);

    /* Create stream over raw TySh payload */
    psd_stream_t *stream = psd_stream_create_buffer(doc->allocator, item->raw_tysh,
                                                    (size_t)item->raw_tysh_len);
    if (!stream) {
        PSD_TL_DEBUG("  Failed to create stream\n");
        return PSD_ERR_OUT_OF_MEMORY;
    }

    psd_status_t status = PSD_OK;
    uint16_t u16 = 0;
    uint32_t u32 = 0;

    /* Re-read TySh fields sequentially (no seeking-by-length):
       1) uint16 tysh_version
       2) 6 x double transform (48 bytes)
       3) uint16 text_version
       4) uint32 text_desc_version
       5) text_data descriptor (ActionDescriptor)
       6) uint16 warp_version
       7) uint32 warp_desc_version
       8) warp_data descriptor (ActionDescriptor)
     */
    status = psd_stream_read_be16(stream, &u16);
    if (status != PSD_OK) goto cleanup;
    item->tysh_version = u16;

    /* Skip 6 doubles */
    status = psd_stream_skip(stream, 48);
    if (status != PSD_OK) goto cleanup;

    status = psd_stream_read_be16(stream, &u16);
    if (status != PSD_OK) goto cleanup;
    item->text_version = u16;

    status = psd_stream_read_be32(stream, &u32);
    if (status != PSD_OK) goto cleanup;
    item->text_desc_version = u32;

    /* Parse text_data descriptor */
    PSD_TL_DEBUG("  Parsing text_data descriptor...\n");
    status = psd_parse_descriptor(stream, doc->allocator, doc->is_psb, &item->text_data);
    if (status != PSD_OK) {
        PSD_TL_DEBUG("  Failed to parse text_data: status=%d\n", status);
        goto cleanup;
    }

    PSD_TL_DEBUG("  text_data parsed: class_id=%s, properties=%zu\n",
            item->text_data && item->text_data->class_id ? item->text_data->class_id : "(empty)",
            item->text_data ? item->text_data->property_count : 0);

    /* Warp descriptor: some files may omit it.
       If we can't read the warp header due to EOF, that's not fatal for text. */
    status = psd_stream_read_be16(stream, &u16);
    if (status != PSD_OK) {
        PSD_TL_DEBUG("  No warp header (status=%d), returning text_data only\n", status);
        status = PSD_OK;
        goto cleanup_ok;
    }
    item->warp_version = u16;

    status = psd_stream_read_be32(stream, &u32);
    if (status != PSD_OK) {
        PSD_TL_DEBUG("  No warp desc version (status=%d), returning text_data only\n", status);
        status = PSD_OK;
        goto cleanup_ok;
    }
    item->warp_desc_version = u32;

    PSD_TL_DEBUG("  Parsing warp_data descriptor...\n");
    status = psd_parse_descriptor(stream, doc->allocator, doc->is_psb, &item->warp_data);
    if (status != PSD_OK) {
        /* warp is optional */
        PSD_TL_DEBUG("  Failed to parse warp_data: status=%d (non-fatal)\n", status);
        status = PSD_OK;
    }

    PSD_TL_DEBUG("  Descriptors parsed OK\n");

cleanup:
    if (status != PSD_OK) {
        if (item->text_data) {
            psd_descriptor_free(item->text_data, doc->allocator);
            item->text_data = NULL;
        }
        if (item->warp_data) {
            psd_descriptor_free(item->warp_data, doc->allocator);
            item->warp_data = NULL;
        }
    }
    psd_stream_destroy(stream);
    return status;

cleanup_ok:
    psd_stream_destroy(stream);
    return status;
}

static psd_status_t psd_descriptor_find_string_recursive(
    const psd_descriptor_t *desc,
    const char *key,
    const char **out_str)
{
    if (!desc || !key || !out_str) {
        return PSD_ERR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < desc->property_count; i++) {
        const psd_descriptor_property_t *prop = &desc->properties[i];
        if (prop->key && strcmp(prop->key, key) == 0) {
            if (prop->value.type_id == PSD_DESC_STRING && prop->value.raw_data) {
                *out_str = (const char *)prop->value.raw_data;
                return PSD_OK;
            }
            /* key exists but not a string */
            return PSD_ERR_INVALID_STRUCTURE;
        }

        /* recurse into objects/lists */
        if (prop->value.object) {
            psd_status_t st = psd_descriptor_find_string_recursive(prop->value.object, key, out_str);
            if (st == PSD_OK) return PSD_OK;
        }
        if (prop->value.list) {
            for (size_t j = 0; j < prop->value.list->item_count; j++) {
                const psd_descriptor_value_t *v = &prop->value.list->items[j];
                if (v->object) {
                    psd_status_t st = psd_descriptor_find_string_recursive(v->object, key, out_str);
                    if (st == PSD_OK) return PSD_OK;
                }
            }
        }
    }

    return PSD_ERR_INVALID_STRUCTURE; /* not found */
}

static psd_status_t psd_descriptor_find_raw_recursive(
    const psd_descriptor_t *desc,
    const char *key,
    const uint8_t **out_data,
    uint64_t *out_len,
    uint32_t *out_type)
{
    if (!desc || !key || !out_data || !out_len) {
        return PSD_ERR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < desc->property_count; i++) {
        const psd_descriptor_property_t *prop = &desc->properties[i];
        if (prop->key && strcmp(prop->key, key) == 0) {
            if (prop->value.raw_data && prop->value.data_length > 0) {
                *out_data = (const uint8_t *)prop->value.raw_data;
                *out_len = prop->value.data_length;
                if (out_type) *out_type = prop->value.type_id;
                return PSD_OK;
            }
            return PSD_ERR_INVALID_STRUCTURE;
        }
        if (prop->value.object) {
            psd_status_t st = psd_descriptor_find_raw_recursive(prop->value.object, key, out_data, out_len, out_type);
            if (st == PSD_OK) return PSD_OK;
        }
        if (prop->value.list) {
            for (size_t j = 0; j < prop->value.list->item_count; j++) {
                const psd_descriptor_value_t *v = &prop->value.list->items[j];
                if (v->object) {
                    psd_status_t st = psd_descriptor_find_raw_recursive(v->object, key, out_data, out_len, out_type);
                    if (st == PSD_OK) return PSD_OK;
                }
            }
        }
    }
    return PSD_ERR_INVALID_STRUCTURE;
}

/* The public API no longer exposes font lists / debug tooling.
   Keep EngineData decoding + minimal default-style extraction only. */

/* Convert EngineData blob to UTF-8 readable text by decoding UTF-16 BOM strings in (...) */
static psd_status_t psd_engine_data_to_utf8_text(
    const psd_allocator_t *alloc,
    const uint8_t *data,
    size_t len,
    char *out,
    size_t out_sz)
{
    if (!data || len == 0 || !out || out_sz == 0) {
        return PSD_ERR_INVALID_ARGUMENT;
    }

    size_t o = 0;
    out[0] = '\0';
    psd_status_t result = PSD_OK;

    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];

        /* decode BOM-prefixed UTF-16 strings inside parentheses */
        if (c == '(') {
            /* find closing ')' respecting backslash escapes */
            size_t j = i + 1;
            bool esc = false;
            while (j < len) {
                uint8_t cj = data[j];
                if (!esc && cj == ')') break;
                if (!esc && cj == '\\') { esc = true; j++; continue; }
                esc = false;
                j++;
            }
            if (j >= len) {
                /* unmatched '(' - just copy and stop */
                if (o + 1 >= out_sz) { result = PSD_ERR_BUFFER_TOO_SMALL; break; }
                out[o++] = '(';
                out[o] = '\0';
                result = PSD_ERR_INVALID_FORMAT;
                break;
            }

            size_t inner_len = (j > i + 1) ? (j - (i + 1)) : 0;
            const uint8_t *inner = data + i + 1;

            /* write '(' */
            if (o + 1 >= out_sz) { result = PSD_ERR_BUFFER_TOO_SMALL; break; }
            out[o++] = '(';

            if (inner_len >= 2 && inner[0] == 0xFE && inner[1] == 0xFF) {
                /* UTF-16BE content */
                size_t utf8_len = 0;
                uint8_t *utf8 = psd_utf16be_to_utf8(alloc, inner + 2, inner_len - 2, &utf8_len);
                if (utf8) {
                    size_t to_copy = utf8_len;
                    if (o + to_copy + 2 >= out_sz) {
                        to_copy = (out_sz > o + 2) ? (out_sz - o - 2) : 0;
                        result = PSD_ERR_BUFFER_TOO_SMALL;
                    }
                    if (to_copy > 0) {
                        memcpy(out + o, utf8, to_copy);
                        o += to_copy;
                    }
                    psd_alloc_free(alloc, utf8);
                }
            } else if (inner_len >= 2 && inner[0] == 0xFF && inner[1] == 0xFE) {
                /* UTF-16LE content: swap to BE then decode */
                uint8_t *sw = (uint8_t *)psd_alloc_malloc(alloc, inner_len - 2);
                if (sw) {
                    size_t n = (inner_len - 2) & ~1u;
                    for (size_t k = 0; k < n; k += 2) {
                        sw[k] = inner[2 + k + 1];
                        sw[k + 1] = inner[2 + k];
                    }
                    size_t utf8_len = 0;
                    uint8_t *utf8 = psd_utf16be_to_utf8(alloc, sw, inner_len - 2, &utf8_len);
                    psd_alloc_free(alloc, sw);
                    if (utf8) {
                        size_t to_copy = utf8_len;
                        if (o + to_copy + 2 >= out_sz) {
                            to_copy = (out_sz > o + 2) ? (out_sz - o - 2) : 0;
                            result = PSD_ERR_BUFFER_TOO_SMALL;
                        }
                        if (to_copy > 0) {
                            memcpy(out + o, utf8, to_copy);
                            o += to_copy;
                        }
                        psd_alloc_free(alloc, utf8);
                    }
                }
            } else {
                /* treat as UTF-8/ASCII bytes */
                size_t to_copy = inner_len;
                if (o + to_copy + 2 >= out_sz) {
                    to_copy = (out_sz > o + 2) ? (out_sz - o - 2) : 0;
                    result = PSD_ERR_BUFFER_TOO_SMALL;
                }
                if (to_copy > 0) {
                    memcpy(out + o, inner, to_copy);
                    o += to_copy;
                }
            }

            /* write ')' */
            if (o + 1 >= out_sz) { result = PSD_ERR_BUFFER_TOO_SMALL; break; }
            out[o++] = ')';
            out[o] = '\0';

            i = j; /* continue after ')' */
            continue;
        }

        /* default byte copy */
        if (o + 1 >= out_sz) {
            result = PSD_ERR_BUFFER_TOO_SMALL;
            break;
        }
        out[o++] = (char)c;
        out[o] = '\0';
    }

    return result;
}

static psd_text_layer_t *psd_find_text_layer_mut(psd_document_t *doc, uint32_t layer_index)
{
    if (!doc || !doc->text_layers.items || doc->text_layers.count == 0) {
        return NULL;
    }
    for (size_t i = 0; i < doc->text_layers.count; i++) {
        if (doc->text_layers.items[i].layer_index == layer_index) {
            return &doc->text_layers.items[i];
        }
    }
    return NULL;
}

static psd_status_t psd_text_layer_get_engine_data_raw(
    psd_document_t *doc,
    uint32_t layer_index,
    const uint8_t **data,
    uint64_t *length)
{
    if (!doc) return PSD_ERR_NULL_POINTER;
    if (data) *data = NULL;
    if (length) *length = 0;

    psd_text_layer_t *text_layer = psd_find_text_layer_mut(doc, layer_index);
    if (!text_layer) return PSD_ERR_CORRUPT_DATA;

    psd_status_t st = psd_text_layer_ensure_descriptors_parsed(doc, text_layer);
    if (st != PSD_OK) return st;

    const uint8_t *raw = NULL;
    uint64_t raw_len = 0;
    uint32_t raw_type = 0;
    st = psd_descriptor_find_raw_recursive(text_layer->text_data, "EngineData", &raw, &raw_len, &raw_type);
    if (st != PSD_OK) return st;

    if (data) *data = raw;
    if (length) *length = raw_len;
    return PSD_OK;
}

/**
 * @brief Get text layer information by index
 *
 * Linear scan of text layers to find one matching the given layer_index.
 *
 * @param doc Document to query
 * @param layer_index Layer index to find
 * @return Pointer to text layer record if found, NULL otherwise
 */
/**
 * @brief Extract text content from a text layer
 *
 * Parses the TySh descriptor and extracts the actual text string.
 *
 * @param doc Document containing the text layer
 * @param layer_index Layer index (0-based)
 * @param buffer Buffer to store extracted text
 * @param buffer_size Size of buffer
 * @return PSD_OK on success, negative error code on failure
 */
psd_status_t psd_text_layer_get_text(
    psd_document_t *doc,
    uint32_t layer_index,
    char *buffer,
    size_t buffer_size)
{
    if (!doc || !buffer || buffer_size == 0) {
        return PSD_ERR_NULL_POINTER;
    }

    buffer[0] = '\0';

    /* Get the text layer (cast away const for lazy-parse mutation) */
    psd_text_layer_t *text_layer = psd_find_text_layer_mut(doc, layer_index);
    if (!text_layer) {
        return PSD_ERR_CORRUPT_DATA;
    }

    /* Lazy-parse descriptors on first access */
    psd_status_t status = psd_text_layer_ensure_descriptors_parsed(doc, text_layer);
    if (status != PSD_OK) {
        PSD_TL_DEBUG("DEBUG: Lazy-parse failed: status=%d\n", status);
        return status;
    }

    /* Now look for "Txt " property in the parsed text_data descriptor */
    if (!text_layer->text_data) {
        return PSD_ERR_CORRUPT_DATA;
    }

    const char *s = NULL;
    status = psd_descriptor_find_string_recursive(text_layer->text_data, "Txt ", &s);
    if (status != PSD_OK || !s) {
        return status; /* not found is a clean non-parse error */
    }

    size_t slen = strlen(s);
    size_t copy_len = (slen < (buffer_size - 1)) ? slen : (buffer_size - 1);
    memcpy(buffer, s, copy_len);
    buffer[copy_len] = '\0';
    return PSD_OK;
}

static const char *psd_find_token(const char *s, const char *token)
{
    if (!s || !token) return NULL;
    return strstr(s, token);
}

static const char *psd_skip_ws(const char *p)
{
    while (p && *p && isspace((unsigned char)*p)) p++;
    return p;
}

static bool psd_parse_int_after(const char *s, const char *token, int *out_val)
{
    const char *p = psd_find_token(s, token);
    if (!p) return false;
    p += strlen(token);
    p = psd_skip_ws(p);
    if (!p || !*p) return false;
    char *endp = NULL;
    long v = strtol(p, &endp, 10);
    if (endp == p) return false;
    *out_val = (int)v;
    return true;
}

static bool psd_parse_double_after(const char *s, const char *token, double *out_val)
{
    const char *p = psd_find_token(s, token);
    if (!p) return false;
    p += strlen(token);
    p = psd_skip_ws(p);
    if (!p || !*p) return false;
    char *endp = NULL;
    double v = strtod(p, &endp);
    if (endp == p) return false;
    *out_val = v;
    return true;
}

static bool psd_parse_rgb_after_fillcolor(const char *s, uint8_t out_rgba[4])
{
    const char *p = psd_find_token(s, "/FillColor");
    if (!p) return false;

    /* Prefer "Values [ ... ]" if present nearby */
    const char *v = strstr(p, "Values");
    if (v) p = v;

    const char *br = strchr(p, '[');
    if (!br) return false;
    br++;

    br = psd_skip_ws(br);
    char *endp = NULL;
    double r = strtod(br, &endp);
    if (endp == br) return false;
    br = psd_skip_ws(endp);
    double g = strtod(br, &endp);
    if (endp == br) return false;
    br = psd_skip_ws(endp);
    double b = strtod(br, &endp);
    if (endp == br) return false;

    /* EngineData typically uses 0..1 floats for RGB */
    if (r < 0) { r = 0; }
    if (r > 1) { r = 1; }
    if (g < 0) { g = 0; }
    if (g > 1) { g = 1; }
    if (b < 0) { b = 0; }
    if (b > 1) { b = 1; }

    out_rgba[0] = (uint8_t)(r * 255.0 + 0.5);
    out_rgba[1] = (uint8_t)(g * 255.0 + 0.5);
    out_rgba[2] = (uint8_t)(b * 255.0 + 0.5);
    out_rgba[3] = 255;
    return true;
}

static size_t psd_extract_fontset_names(const char *s, char names[][PSD_TEXT_FONT_NAME_MAX], size_t max_names)
{
    if (!s || !names || max_names == 0) return 0;

    const char *p = psd_find_token(s, "/FontSet");
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    p++;

    size_t count = 0;
    while (*p && count < max_names) {
        const char *n = psd_find_token(p, "/Name");
        if (!n) break;
        const char *lp = strchr(n, '(');
        const char *rp = lp ? strchr(lp + 1, ')') : NULL;
        if (!lp || !rp) { p = n + 5; continue; }
        size_t len = (size_t)(rp - (lp + 1));
        if (len >= PSD_TEXT_FONT_NAME_MAX) len = PSD_TEXT_FONT_NAME_MAX - 1;
        memcpy(names[count], lp + 1, len);
        names[count][len] = '\0';
        count++;
        p = rp + 1;
    }

    return count;
}

psd_status_t psd_text_layer_get_matrix_bounds(
    psd_document_t *doc,
    uint32_t layer_index,
    psd_text_matrix_t *out_matrix,
    psd_text_bounds_t *out_bounds)
{
    if (!doc || !out_matrix || !out_bounds) {
        return PSD_ERR_NULL_POINTER;
    }

    psd_text_layer_t *text_layer = psd_find_text_layer_mut(doc, layer_index);
    if (!text_layer) {
        return PSD_ERR_CORRUPT_DATA;
    }

    out_matrix->xx = text_layer->transform.xx;
    out_matrix->xy = text_layer->transform.xy;
    out_matrix->yx = text_layer->transform.yx;
    out_matrix->yy = text_layer->transform.yy;
    out_matrix->tx = text_layer->transform.tx;
    out_matrix->ty = text_layer->transform.ty;

    out_bounds->left = text_layer->text_bounds.left;
    out_bounds->top = text_layer->text_bounds.top;
    out_bounds->right = text_layer->text_bounds.right;
    out_bounds->bottom = text_layer->text_bounds.bottom;

    return PSD_OK;
}

psd_status_t psd_text_layer_get_default_style(
    psd_document_t *doc,
    uint32_t layer_index,
    psd_text_style_t *out_style)
{
    if (!doc || !out_style) {
        return PSD_ERR_NULL_POINTER;
    }

    memset(out_style, 0, sizeof(*out_style));
    out_style->color_rgba[3] = 255;
    out_style->justification = PSD_TEXT_JUSTIFY_LEFT;

    const uint8_t *engine = NULL;
    uint64_t engine_len = 0;
    psd_status_t st = psd_text_layer_get_engine_data_raw(doc, layer_index, &engine, &engine_len);
    if (st != PSD_OK || !engine || engine_len == 0) {
        return (st == PSD_OK) ? PSD_ERR_INVALID_STRUCTURE : st;
    }

    /* Convert EngineData to readable UTF-8 text (decoding BOM UTF-16 strings in parentheses) */
    char engine_txt[8192] = {0};
    psd_status_t st_txt = psd_engine_data_to_utf8_text(doc->allocator, engine, (size_t)engine_len, engine_txt, sizeof(engine_txt));
    if (st_txt != PSD_OK && st_txt != PSD_ERR_BUFFER_TOO_SMALL) {
        return st_txt;
    }

    /* Font name: try StyleSheet "Font <idx>" + FontSet names */
    int font_idx = -1;
    (void)psd_parse_int_after(engine_txt, "/Font", &font_idx);

    char font_names[64][PSD_TEXT_FONT_NAME_MAX];
    memset(font_names, 0, sizeof(font_names));
    size_t font_count = psd_extract_fontset_names(engine_txt, font_names, 64);
    if (font_count > 0) {
        size_t pick = 0;
        if (font_idx >= 0 && (size_t)font_idx < font_count) {
            pick = (size_t)font_idx;
        }
        strncpy(out_style->font_name, font_names[pick], sizeof(out_style->font_name) - 1);
        out_style->font_name[sizeof(out_style->font_name) - 1] = '\0';
    } else {
        /* Fallback: first occurrence of any "(...)" after "/Name" */
        const char *n = psd_find_token(engine_txt, "/Name");
        const char *lp = n ? strchr(n, '(') : NULL;
        const char *rp = lp ? strchr(lp + 1, ')') : NULL;
        if (lp && rp && rp > lp + 1) {
            size_t len = (size_t)(rp - (lp + 1));
            if (len >= sizeof(out_style->font_name)) len = sizeof(out_style->font_name) - 1;
            memcpy(out_style->font_name, lp + 1, len);
            out_style->font_name[len] = '\0';
        }
    }

    /* Size */
    (void)psd_parse_double_after(engine_txt, "/FontSize", &out_style->size);

    /* Tracking */
    (void)psd_parse_double_after(engine_txt, "/Tracking", &out_style->tracking);

    /* Leading: if explicit Leading exists, use it; otherwise, try AutoLeading multiplier */
    if (!psd_parse_double_after(engine_txt, "/Leading", &out_style->leading)) {
        double auto_leading = 0;
        if (psd_parse_double_after(engine_txt, "/AutoLeading", &auto_leading) && out_style->size > 0 && auto_leading > 0) {
            out_style->leading = out_style->size * auto_leading;
        }
    }

    /* Justification */
    int just = 0;
    if (psd_parse_int_after(engine_txt, "/Justification", &just)) {
        switch (just) {
            case 0: out_style->justification = PSD_TEXT_JUSTIFY_LEFT; break;
            case 1: out_style->justification = PSD_TEXT_JUSTIFY_RIGHT; break;
            case 2: out_style->justification = PSD_TEXT_JUSTIFY_CENTER; break;
            case 3: out_style->justification = PSD_TEXT_JUSTIFY_FULL; break;
            default: out_style->justification = PSD_TEXT_JUSTIFY_LEFT; break;
        }
    }

    /* Color */
    (void)psd_parse_rgb_after_fillcolor(engine_txt, out_style->color_rgba);

    /* Require minimal fields for phase-1 rendering */
    if (out_style->font_name[0] == '\0' || out_style->size <= 0.0) {
        return PSD_ERR_INVALID_STRUCTURE;
    }

    return PSD_OK;
}
