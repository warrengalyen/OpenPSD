/**
 * @file psd_descriptor.c
 * @brief ActionDescriptor parsing implementation
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

#include "psd_descriptor.h"
#include "../include/openpsd/psd_stream.h"
#include "psd_endian.h"
#include "psd_alloc.h"
#include "psd_unicode.h"
#include <string.h>
#include <stdlib.h>

static void psd_descriptor_list_free(
    psd_descriptor_list_t *list,
    const psd_allocator_t *allocator);

/**
 * @brief Parse a Unicode string from stream
 *
 * Format: 4-byte length (big-endian) + UTF-16BE characters
 */
static psd_status_t psd_parse_unicode_string(
    psd_stream_t *stream,
    const psd_allocator_t *allocator,
    char **out_string)
{
    psd_status_t status;
    uint32_t char_count;
    
    /* Read character count */
    status = psd_stream_read_be32(stream, &char_count);
    if (status != PSD_OK) {
        return status;
    }
    
    if (char_count == 0) {
        *out_string = (char *)psd_alloc_malloc(allocator, 1);
        if (!*out_string) {
            return PSD_ERR_OUT_OF_MEMORY;
        }
        (*out_string)[0] = '\0';
        return PSD_OK;
    }
    
    /* Read UTF-16BE bytes and convert to UTF-8 */
    size_t utf16_bytes = (size_t)char_count * 2;
    uint8_t *utf16 = (uint8_t *)psd_alloc_malloc(allocator, utf16_bytes);
    if (!utf16) {
        return PSD_ERR_OUT_OF_MEMORY;
    }
    
    int64_t read_bytes = psd_stream_read(stream, utf16, utf16_bytes);
    if ((size_t)read_bytes != utf16_bytes) {
        psd_alloc_free(allocator, utf16);
        return PSD_ERR_STREAM_EOF;
    }
    
    size_t utf8_len = 0;
    uint8_t *utf8 = psd_utf16be_to_utf8(allocator, utf16, utf16_bytes, &utf8_len);
    psd_alloc_free(allocator, utf16);
    if (!utf8) {
        return PSD_ERR_OUT_OF_MEMORY;
    }
    *out_string = (char *)utf8;
    return PSD_OK;
}

/**
 * @brief Skip a Unicode string from stream without allocating
 *
 * Format: 4-byte length (big-endian) + UTF-16BE characters
 */
static psd_status_t psd_skip_unicode_string(psd_stream_t *stream)
{
    psd_status_t status;
    uint32_t char_count = 0;
    status = psd_stream_read_be32(stream, &char_count);
    if (status != PSD_OK) {
        return status;
    }
    if (char_count == 0) {
        return PSD_OK;
    }
    /* Sanity: prevent absurd skips / overflow */
    if (char_count > 1000000u) {
        return PSD_ERR_CORRUPT_DATA;
    }
    size_t bytes = (size_t)char_count * 2;
    return psd_stream_skip(stream, bytes);
}

/**
 * @brief Parse a ClassID / Key token from stream
 *
 * Photoshop ActionDescriptor format uses "ClassID" tokens:
 * - 4-byte length (big-endian)
 * - if length == 0, the ID is a 4-byte OSType (e.g. 'TxLr', 'Txt ')
 * - else, the ID is an ASCII string of that length
 *
 * Returned string is NUL-terminated and owned by allocator.
 */
static psd_status_t psd_parse_class_id(
    psd_stream_t *stream,
    const psd_allocator_t *allocator,
    char **out_string)
{
    psd_status_t status;
    uint32_t length = 0;

    if (!out_string) {
        return PSD_ERR_INVALID_ARGUMENT;
    }
    *out_string = NULL;

    status = psd_stream_read_be32(stream, &length);
    if (status != PSD_OK) {
        return status;
    }

    if (length == 0) {
        uint8_t ostype[4];
        status = psd_stream_read_exact(stream, ostype, 4);
        if (status != PSD_OK) {
            return status;
        }
        char *s = (char *)psd_alloc_malloc(allocator, 5);
        if (!s) {
            return PSD_ERR_OUT_OF_MEMORY;
        }
        s[0] = (char)ostype[0];
        s[1] = (char)ostype[1];
        s[2] = (char)ostype[2];
        s[3] = (char)ostype[3];
        s[4] = '\0';
        *out_string = s;
        return PSD_OK;
    }

    /* Non-zero length: ASCII bytes follow. Reuse shared ASCII parser. */
    {
        /* psd_parse_ascii_string reads its own length; we've already consumed it.
           So we implement a small inline read here but via the same logic. */
        uint8_t *buffer = (uint8_t *)psd_alloc_malloc(allocator, (size_t)length + 1);
        if (!buffer) {
            return PSD_ERR_OUT_OF_MEMORY;
        }
        if (length > 0) {
            int64_t read_bytes2 = psd_stream_read(stream, buffer, (size_t)length);
            if ((uint32_t)read_bytes2 != length) {
                psd_alloc_free(allocator, buffer);
                return PSD_ERR_STREAM_EOF;
            }
        }
        buffer[length] = '\0';
        *out_string = (char *)buffer;
        return PSD_OK;
    }
}

/**
 * @brief Parse a descriptor value from stream
 *
 * Reads the raw bytes of a value without interpretation.
 * The type ID is already read by the caller.
 */
static psd_status_t psd_parse_descriptor_value(
    psd_stream_t *stream,
    uint32_t type_id,
    const psd_allocator_t *allocator,
    psd_descriptor_value_t *value,
    bool is_psb)
{
    psd_status_t status;
    
    value->type_id = type_id;
    value->raw_data = NULL;
    value->data_length = 0;
    value->object = NULL;
    value->list = NULL;
    
    /* Different types have different lengths */
    switch (type_id) {
        case PSD_DESC_INTEGER:     /* long - 4 bytes */
        case PSD_DESC_DOUBLE: {    /* doub - 8 bytes */
            uint64_t size = (type_id == PSD_DESC_INTEGER) ? 4 : 8;
            value->raw_data = (uint8_t *)psd_alloc_malloc(allocator, size);
            if (!value->raw_data) {
                return PSD_ERR_OUT_OF_MEMORY;
            }
            value->data_length = size;
            
            int64_t read_bytes = psd_stream_read(stream, value->raw_data, size);
            if ((uint64_t)read_bytes != size) {
                psd_alloc_free(allocator, value->raw_data);
                value->raw_data = NULL;
                return PSD_ERR_STREAM_EOF;
            }
            break;
        }
        
        case PSD_DESC_UNIT_FLOAT: {  /* UntF - 4-byte unit + 8-byte double */
            value->raw_data = (uint8_t *)psd_alloc_malloc(allocator, 12);
            if (!value->raw_data) {
                return PSD_ERR_OUT_OF_MEMORY;
            }
            value->data_length = 12;
            
            int64_t read_bytes = psd_stream_read(stream, value->raw_data, 12);
            if (read_bytes != 12) {
                psd_alloc_free(allocator, value->raw_data);
                value->raw_data = NULL;
                return PSD_ERR_STREAM_EOF;
            }
            break;
        }

        case PSD_DESC_UNIT_VALUE: { /* UntV - treat same layout as UntF for now */
            value->raw_data = (uint8_t *)psd_alloc_malloc(allocator, 12);
            if (!value->raw_data) {
                return PSD_ERR_OUT_OF_MEMORY;
            }
            value->data_length = 12;

            int64_t read_bytes = psd_stream_read(stream, value->raw_data, 12);
            if (read_bytes != 12) {
                psd_alloc_free(allocator, value->raw_data);
                value->raw_data = NULL;
                return PSD_ERR_STREAM_EOF;
            }
            break;
        }
        
        case PSD_DESC_BOOLEAN: {     /* bool - 1 byte */
            value->raw_data = (uint8_t *)psd_alloc_malloc(allocator, 1);
            if (!value->raw_data) {
                return PSD_ERR_OUT_OF_MEMORY;
            }
            value->data_length = 1;
            
            int64_t read_bytes = psd_stream_read(stream, value->raw_data, 1);
            if (read_bytes != 1) {
                psd_alloc_free(allocator, value->raw_data);
                value->raw_data = NULL;
                return PSD_ERR_STREAM_EOF;
            }
            break;
        }
        
        case PSD_DESC_STRING: {      /* TEXT - Unicode string */
            status = psd_parse_unicode_string(stream, allocator, (char **)&value->raw_data);
            if (status != PSD_OK) {
                return status;
            }
            value->data_length = value->raw_data ? (uint64_t)strlen((char *)value->raw_data) : 0;
            break;
        }
        
        case PSD_DESC_ENUMERATED: {  /* enum - EnumType(ClassID) + EnumValue(ClassID) */
            char *enum_type = NULL;
            char *enum_value = NULL;
            status = psd_parse_class_id(stream, allocator, &enum_type);
            if (status != PSD_OK) {
                return status;
            }
            status = psd_parse_class_id(stream, allocator, &enum_value);
            if (status != PSD_OK) {
                psd_alloc_free(allocator, enum_type);
                return status;
            }

            /* Store a simple "type:value" string for debugging/inspection */
            size_t lt = strlen(enum_type);
            size_t lv = strlen(enum_value);
            size_t out_len = lt + 1 + lv;
            char *out = (char *)psd_alloc_malloc(allocator, out_len + 1);
            if (!out) {
                psd_alloc_free(allocator, enum_type);
                psd_alloc_free(allocator, enum_value);
                return PSD_ERR_OUT_OF_MEMORY;
            }
            memcpy(out, enum_type, lt);
            out[lt] = ':';
            memcpy(out + lt + 1, enum_value, lv);
            out[out_len] = '\0';

            psd_alloc_free(allocator, enum_type);
            psd_alloc_free(allocator, enum_value);

            value->raw_data = (uint8_t *)out;
            value->data_length = (uint64_t)out_len;
            break;
        }

        case PSD_DESC_REFERENCE: { /* 'ref ' - Reference (complex). Skip. */
            uint32_t item_count = 0;
            status = psd_stream_read_be32(stream, &item_count);
            if (status != PSD_OK) {
                return status;
            }
            if (item_count > 1000000u) {
                return PSD_ERR_CORRUPT_DATA;
            }
            for (uint32_t i = 0; i < item_count; i++) {
                uint32_t ref_type = 0;
                status = psd_stream_read_be32(stream, &ref_type);
                if (status != PSD_OK) {
                    return status;
                }

                /* Many reference forms exist; implement the most common tokens. */
                switch (ref_type) {
                    case 0x70726F70: /* 'prop' */
                        /* ClassID + Key(ClassID) */
                        {
                            char *tmp = NULL;
                            status = psd_parse_class_id(stream, allocator, &tmp);
                            if (status != PSD_OK) return status;
                            psd_alloc_free(allocator, tmp);
                            status = psd_parse_class_id(stream, allocator, &tmp);
                            if (status != PSD_OK) return status;
                            psd_alloc_free(allocator, tmp);
                        }
                        break;
                    case 0x436C7373: /* 'Clss' */
                        /* ClassID */
                        {
                            char *tmp = NULL;
                            status = psd_parse_class_id(stream, allocator, &tmp);
                            if (status != PSD_OK) return status;
                            psd_alloc_free(allocator, tmp);
                        }
                        break;
                    case 0x456E6D72: /* 'Enmr' */
                        /* ClassID + EnumType(ClassID) + EnumValue(ClassID) */
                        {
                            char *tmp = NULL;
                            status = psd_parse_class_id(stream, allocator, &tmp);
                            if (status != PSD_OK) return status;
                            psd_alloc_free(allocator, tmp);
                            status = psd_parse_class_id(stream, allocator, &tmp);
                            if (status != PSD_OK) return status;
                            psd_alloc_free(allocator, tmp);
                            status = psd_parse_class_id(stream, allocator, &tmp);
                            if (status != PSD_OK) return status;
                            psd_alloc_free(allocator, tmp);
                        }
                        break;
                    case 0x49646E74: /* 'Idnt' */
                        /* uint32 identifier */
                        {
                            uint32_t id = 0;
                            status = psd_stream_read_be32(stream, &id);
                            if (status != PSD_OK) return status;
                        }
                        break;
                    case 0x696E6478: /* 'indx' */
                        /* uint32 index */
                        {
                            uint32_t idx = 0;
                            status = psd_stream_read_be32(stream, &idx);
                            if (status != PSD_OK) return status;
                        }
                        break;
                    case 0x6E616D65: /* 'name' */
                        /* Unicode string */
                        {
                            char *tmp = NULL;
                            status = psd_parse_unicode_string(stream, allocator, &tmp);
                            if (status != PSD_OK) return status;
                            psd_alloc_free(allocator, tmp);
                        }
                        break;
                    default:
                        /* Unknown reference form: fail gracefully */
                        return PSD_ERR_UNSUPPORTED_FEATURE;
                }
            }

            /* We don't preserve ref structure yet */
            value->raw_data = NULL;
            value->data_length = 0;
            break;
        }

        case PSD_DESC_RAW_DATA: { /* raws - 4-byte length + data */
            uint32_t len = 0;
            status = psd_stream_read_be32(stream, &len);
            if (status != PSD_OK) {
                return status;
            }
            value->raw_data = (uint8_t *)psd_alloc_malloc(allocator, (size_t)len);
            if (!value->raw_data && len > 0) {
                return PSD_ERR_OUT_OF_MEMORY;
            }
            value->data_length = len;
            if (len > 0) {
                int64_t read_bytes = psd_stream_read(stream, value->raw_data, (size_t)len);
                if ((uint32_t)read_bytes != len) {
                    psd_alloc_free(allocator, value->raw_data);
                    value->raw_data = NULL;
                    return PSD_ERR_STREAM_EOF;
                }
            }
            break;
        }

        case PSD_DESC_CLASS: { /* type - ClassID token */
            char *cid = NULL;
            status = psd_parse_class_id(stream, allocator, &cid);
            if (status != PSD_OK) {
                return status;
            }
            value->raw_data = (uint8_t *)cid;
            value->data_length = (uint64_t)strlen(cid);
            break;
        }

        case PSD_DESC_OBJECT: { /* Obj  - classID token + descriptor */
            /* Object values vary in the wild. Try two common layouts:
               A) Unicode name + ClassID + Descriptor
               B) ClassID + Descriptor
             */
            int64_t start = psd_stream_tell(stream);
            if (start < 0) {
                return PSD_ERR_STREAM_INVALID;
            }

            /* Attempt A */
            {
                char *obj_class = NULL;
                psd_descriptor_t *obj_desc = NULL;

                psd_status_t st = psd_skip_unicode_string(stream);
                if (st == PSD_OK) {
                    st = psd_parse_class_id(stream, allocator, &obj_class);
                }
                if (st == PSD_OK) {
                    st = psd_parse_descriptor(stream, allocator, is_psb, &obj_desc);
                }

                if (st == PSD_OK) {
                    value->raw_data = (uint8_t *)obj_class;
                    value->data_length = (uint64_t)strlen(obj_class);
                    value->object = obj_desc;
                    break;
                }

                if (obj_class) psd_alloc_free(allocator, obj_class);
                if (obj_desc) psd_descriptor_free(obj_desc, allocator);
            }

            /* Rewind and attempt B */
            if (psd_stream_seek(stream, start) < 0) {
                return PSD_ERR_STREAM_SEEK;
            }

            {
                char *obj_class = NULL;
                psd_descriptor_t *obj_desc = NULL;

                status = psd_parse_class_id(stream, allocator, &obj_class);
                if (status != PSD_OK) {
                    return status;
                }

                status = psd_parse_descriptor(stream, allocator, is_psb, &obj_desc);
                if (status != PSD_OK) {
                    psd_alloc_free(allocator, obj_class);
                    return status;
                }

                value->raw_data = (uint8_t *)obj_class;
                value->data_length = (uint64_t)strlen(obj_class);
                value->object = obj_desc;
                break;
            }
        }

        case PSD_DESC_LIST: { /* VlLs - uint32 count + (type_id + value)* */
            uint32_t count = 0;
            status = psd_stream_read_be32(stream, &count);
            if (status != PSD_OK) {
                return status;
            }
            if (count > 1000000u) {
                return PSD_ERR_CORRUPT_DATA;
            }

            psd_descriptor_list_t *list = (psd_descriptor_list_t *)psd_alloc_malloc(allocator, sizeof(*list));
            if (!list) {
                return PSD_ERR_OUT_OF_MEMORY;
            }
            list->items = NULL;
            list->item_count = 0;

            if (count > 0) {
                list->items = (psd_descriptor_value_t *)psd_alloc_malloc(allocator, count * sizeof(psd_descriptor_value_t));
                if (!list->items) {
                    psd_alloc_free(allocator, list);
                    return PSD_ERR_OUT_OF_MEMORY;
                }
                memset(list->items, 0, count * sizeof(psd_descriptor_value_t));
            }

            for (uint32_t i = 0; i < count; i++) {
                uint32_t item_type = 0;
                status = psd_stream_read_be32(stream, &item_type);
                if (status != PSD_OK) {
                    list->item_count = i;
                    psd_descriptor_list_free(list, allocator);
                    return status;
                }

                status = psd_parse_descriptor_value(stream, item_type, allocator, &list->items[i], is_psb);
                if (status != PSD_OK) {
                    list->item_count = i + 1;
                    psd_descriptor_list_free(list, allocator);
                    return status;
                }
                list->item_count = i + 1;
            }

            value->list = list;
            break;
        }
        
        default: {
            /* Unknown type - read 4-byte length then raw data */
            uint32_t length;
            status = psd_stream_read_be32(stream, &length);
            if (status != PSD_OK) {
                return status;
            }
            
            if (length > 100 * 1024 * 1024) {  /* 100MB sanity check */
                return PSD_ERR_CORRUPT_DATA;
            }
            
            value->raw_data = (uint8_t *)psd_alloc_malloc(allocator, length);
            if (!value->raw_data && length > 0) {
                return PSD_ERR_OUT_OF_MEMORY;
            }
            value->data_length = length;
            
            if (length > 0) {
                int64_t read_bytes = psd_stream_read(stream, value->raw_data, length);
                if ((uint64_t)read_bytes != length) {
                    psd_alloc_free(allocator, value->raw_data);
                    value->raw_data = NULL;
                    return PSD_ERR_STREAM_EOF;
                }
            }
            break;
        }
    }
    
    return PSD_OK;
}

static void psd_descriptor_value_free(
    psd_descriptor_value_t *value,
    const psd_allocator_t *allocator)
{
    if (!value) return;
    if (value->object) {
        psd_descriptor_free(value->object, allocator);
        value->object = NULL;
    }
    if (value->list) {
        psd_descriptor_list_free(value->list, allocator);
        value->list = NULL;
    }
    if (value->raw_data) {
        psd_alloc_free(allocator, value->raw_data);
        value->raw_data = NULL;
    }
    value->data_length = 0;
}

static void psd_descriptor_list_free(
    psd_descriptor_list_t *list,
    const psd_allocator_t *allocator)
{
    if (!list) return;
    if (list->items) {
        for (size_t i = 0; i < list->item_count; i++) {
            psd_descriptor_value_free(&list->items[i], allocator);
        }
        psd_alloc_free(allocator, list->items);
    }
    psd_alloc_free(allocator, list);
}

/**
 * @brief Parse an ActionDescriptor from stream
 */
psd_status_t psd_parse_descriptor(
    psd_stream_t *stream,
    const psd_allocator_t *allocator,
    bool is_psb,
    psd_descriptor_t **out_descriptor)
{
    psd_status_t status;
    psd_descriptor_t *descriptor = NULL;
    
    if (!stream || !out_descriptor) {
        return PSD_ERR_INVALID_ARGUMENT;
    }
    
    /* Allocate descriptor structure */
    descriptor = (psd_descriptor_t *)psd_alloc_malloc(allocator, sizeof(*descriptor));
    if (!descriptor) {
        return PSD_ERR_OUT_OF_MEMORY;
    }
    
    /* Initialize */
    descriptor->class_id = NULL;
    descriptor->properties = NULL;
    descriptor->property_count = 0;

    /* Descriptors appear in two variants:
       A) Unicode name + ClassID + count
       B) ClassID + count
       Try A first; if it fails, rewind and try B.
     */
    int64_t start = psd_stream_tell(stream);
    if (start < 0) {
        psd_alloc_free(allocator, descriptor);
        return PSD_ERR_STREAM_INVALID;
    }

    /* Try A */
    status = psd_skip_unicode_string(stream);
    if (status == PSD_OK) {
        status = psd_parse_class_id(stream, allocator, &descriptor->class_id);
    }

    if (status != PSD_OK) {
        /* Rewind and try B */
        if (psd_stream_seek(stream, start) < 0) {
            psd_alloc_free(allocator, descriptor);
            return PSD_ERR_STREAM_SEEK;
        }
        status = psd_parse_class_id(stream, allocator, &descriptor->class_id);
        if (status != PSD_OK) {
            psd_alloc_free(allocator, descriptor);
            return status;
        }
    }
    
    /* Read property count */
    uint32_t count;
    status = psd_stream_read_be32(stream, &count);
    if (status != PSD_OK) {
        if (descriptor->class_id) psd_alloc_free(allocator, descriptor->class_id);
        psd_alloc_free(allocator, descriptor);
        return status;
    }

    /* Sanity check: avoid absurd allocations */
    if (count > 1000000u) {
        if (descriptor->class_id) psd_alloc_free(allocator, descriptor->class_id);
        psd_alloc_free(allocator, descriptor);
        return PSD_ERR_CORRUPT_DATA;
    }
    
    /* Allocate property array */
    if (count > 0) {
        descriptor->properties = (psd_descriptor_property_t *)psd_alloc_malloc(
            allocator,
            count * sizeof(psd_descriptor_property_t)
        );
        if (!descriptor->properties) {
            if (descriptor->class_id) psd_alloc_free(allocator, descriptor->class_id);
            psd_alloc_free(allocator, descriptor);
            return PSD_ERR_OUT_OF_MEMORY;
        }
        /* IMPORTANT: ensure keys/raw_data pointers start NULL for safe cleanup on error */
        memset(descriptor->properties, 0, count * sizeof(psd_descriptor_property_t));
    }
    
    descriptor->property_count = count;
    
    /* Parse each property */
    uint32_t parsed_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        psd_descriptor_property_t *prop = &descriptor->properties[i];
        /* Defensive init (also covered by memset above) */
        prop->key = NULL;
        prop->value.raw_data = NULL;
        prop->value.data_length = 0;
        prop->value.type_id = 0;
        
        /* Read key token (ClassID) */
        status = psd_parse_class_id(stream, allocator, &prop->key);
        if (status != PSD_OK) {
            goto error;
        }
        
        /* Read value type (4 bytes) */
        uint32_t type_id;
        status = psd_stream_read_be32(stream, &type_id);
        if (status != PSD_OK) {
            goto error;
        }
        
        /* Parse value */
        status = psd_parse_descriptor_value(stream, type_id, allocator, &prop->value, is_psb);
        if (status != PSD_OK) {
            goto error;
        }

        parsed_count++;
    }
    
    *out_descriptor = descriptor;
    return PSD_OK;
    
error:
    /* Free on error */
    if (descriptor->class_id) {
        psd_alloc_free(allocator, descriptor->class_id);
    }
    if (descriptor->properties) {
        /* Only free entries we successfully initialized */
        for (uint32_t i = 0; i < parsed_count; i++) {
            if (descriptor->properties[i].key) {
                psd_alloc_free(allocator, descriptor->properties[i].key);
            }
            psd_descriptor_value_free(&descriptor->properties[i].value, allocator);
        }
        psd_alloc_free(allocator, descriptor->properties);
    }
    psd_alloc_free(allocator, descriptor);
    return status;
}

/**
 * @brief Free a descriptor and all its data
 */
void psd_descriptor_free(
    psd_descriptor_t *descriptor,
    const psd_allocator_t *allocator)
{
    if (!descriptor) {
        return;
    }
    
    if (descriptor->class_id) {
        psd_alloc_free(allocator, descriptor->class_id);
    }
    
    if (descriptor->properties) {
        for (size_t i = 0; i < descriptor->property_count; i++) {
            if (descriptor->properties[i].key) {
                psd_alloc_free(allocator, descriptor->properties[i].key);
            }
            psd_descriptor_value_free(&descriptor->properties[i].value, allocator);
        }
        psd_alloc_free(allocator, descriptor->properties);
    }
    
    psd_alloc_free(allocator, descriptor);
}
