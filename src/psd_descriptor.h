/**
 * @file psd_descriptor.h
 * @brief ActionDescriptor parsing (Photoshop metadata structures)
 *
 * Implements parsing of ActionDescriptors according to Photoshop specification.
 * ActionDescriptors are used for layer effects, text data, and other metadata.
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

#ifndef PSD_DESCRIPTOR_H
#define PSD_DESCRIPTOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../include/openpsd/psd_error.h"
#include "../include/openpsd/psd_stream.h"
#include "../include/openpsd/psd_types.h"
#include "../include/openpsd/psd_export.h"

/**
 * @brief ActionDescriptor value type codes (4-byte identifiers)
 *
 * These are the types of values that can appear in a descriptor.
 * Unknown types are preserved as raw data.
 */
typedef enum {
    /* Common types */
    PSD_DESC_REFERENCE = 0x72656620,    /**< 'ref ' */
    PSD_DESC_ENUMERATED = 0x656E756D,   /**< 'enum' */
    PSD_DESC_UNIT_FLOAT = 0x556E7446,   /**< 'UntF' */
    PSD_DESC_UNIT_VALUE = 0x556E7456,   /**< 'UntV' */
    PSD_DESC_STRING = 0x54455854,       /**< 'TEXT' */
    PSD_DESC_ENUMERATED_REF = 0x656E756D,/**< 'enum' */
    PSD_DESC_DOUBLE = 0x646F7562,       /**< 'doub' */
    PSD_DESC_INTEGER = 0x6C6F6E67,      /**< 'long' */
    PSD_DESC_BOOLEAN = 0x626F6F6C,      /**< 'bool' */
    PSD_DESC_OBJECT = 0x4F626A20,       /**< 'Obj ' */
    PSD_DESC_LIST = 0x566C4C73,         /**< 'VlLs' */
    PSD_DESC_CLASS = 0x74797065,        /**< 'type' */
    PSD_DESC_RAW_DATA = 0x72617773,     /**< 'raws' */
} psd_descriptor_type_t;

/* Forward declarations for recursive descriptor containers */
typedef struct psd_descriptor psd_descriptor_t;
typedef struct psd_descriptor_list psd_descriptor_list_t;

/**
 * @brief ActionDescriptor value (can be of various types)
 *
 * Values are stored as raw bytes to preserve forward compatibility.
 * Known types can be interpreted by accessor functions.
 */
typedef struct {
    uint32_t type_id;          /**< Type code (4 bytes) */
    uint8_t *raw_data;         /**< Raw value bytes (owned by allocator) */
    uint64_t data_length;      /**< Length of raw data */
    /* Parsed forms for container types (internal). Only populated for Obj/VlLs. */
    psd_descriptor_t *object;         /**< Parsed object descriptor if type is 'Obj ' */
    psd_descriptor_list_t *list;      /**< Parsed list if type is 'VlLs' */
} psd_descriptor_value_t;

/**
 * @brief ActionDescriptor property (key-value pair)
 */
typedef struct {
    char *key;                 /**< Property key (ASCII/UTF-8, NULL-terminated) */
    psd_descriptor_value_t value;  /**< Property value */
} psd_descriptor_property_t;

/**
 * @brief ActionDescriptor (key-value container)
 *
 * A descriptor is essentially a map of keys to values of various types.
 * Used for layer effects, text properties, and other metadata.
 */
typedef struct psd_descriptor {
    char *class_id;            /**< Class identifier (4 bytes, NULL-terminated) */
    psd_descriptor_property_t *properties;  /**< Array of key-value pairs */
    size_t property_count;     /**< Number of properties */
} psd_descriptor_t;

/**
 * @brief ActionList (ordered collection of values)
 *
 * Similar to a descriptor but without keys, just an array of values.
 */
typedef struct psd_descriptor_list {
    psd_descriptor_value_t *items;  /**< Array of values */
    size_t item_count;              /**< Number of items */
} psd_descriptor_list_t;

/**
 * @brief Parse an ActionDescriptor from stream (internal)
 */
PSD_INTERNAL psd_status_t psd_parse_descriptor(
    psd_stream_t *stream,
    const psd_allocator_t *allocator,
    bool is_psb,
    psd_descriptor_t **out_descriptor);

/**
 * @brief Free a descriptor and all its data (internal)
 */
PSD_INTERNAL void psd_descriptor_free(
    psd_descriptor_t *descriptor,
    const psd_allocator_t *allocator);

#endif /* PSD_DESCRIPTOR_H */
