/**
 * @file psd_types.h
 * @brief Core type definitions for library
 *
 * Defines all data structures and types used throughout the library.
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

#ifndef PSD_TYPES_H
#define PSD_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Boolean type for API returns (compatibility with C89)
 */
typedef _Bool psd_bool_t;

/**
 * @brief Represents a byte buffer with length
 *
 * Used for passing binary data without using FILE* pointers.
 * Does not own the memory it points to.
 */
typedef struct {
    const uint8_t *data;   /**< Pointer to buffer data */
    size_t length;         /**< Number of bytes in buffer */
} psd_buffer_t;

/**
 * @brief Mutable byte buffer with length
 *
 * Similar to psd_buffer_t but for writable buffers.
 */
typedef struct {
    uint8_t *data;   /**< Pointer to buffer data */
    size_t length;   /**< Number of bytes in buffer */
} psd_mutable_buffer_t;

/**
 * @brief Dimensions (width and height)
 */
typedef struct {
    uint32_t width;   /**< Width in pixels */
    uint32_t height;  /**< Height in pixels */
} psd_dimensions_t;

/**
 * @brief Rectangle region
 */
typedef struct {
    int32_t top;      /**< Top coordinate */
    int32_t left;     /**< Left coordinate */
    int32_t bottom;   /**< Bottom coordinate */
    int32_t right;    /**< Right coordinate */
} psd_rect_t;

/**
 * @brief Color space enumeration
 *
 * Represents the color model used in the PSD file.
 */
typedef enum {
    PSD_COLOR_BITMAP = 0,      /**< Bitmap (1-bit) */
    PSD_COLOR_GRAYSCALE = 1,   /**< Grayscale */
    PSD_COLOR_INDEXED = 2,     /**< Indexed color */
    PSD_COLOR_RGB = 3,         /**< RGB color */
    PSD_COLOR_CMYK = 4,        /**< CMYK color */
    PSD_COLOR_MULTICHANNEL = 7, /**< Multichannel */
    PSD_COLOR_DUOTONE = 8,     /**< Duotone */
    PSD_COLOR_LAB = 9,         /**< Lab color */
} psd_color_mode_t;

/**
 * @brief Compression method enumeration
 *
 * Specifies how the image data is compressed.
 */
typedef enum {
    PSD_COMPRESSION_RAW = 0,        /**< No compression (raw data) */
    PSD_COMPRESSION_RLE = 1,        /**< RLE compression (PackBits) */
    PSD_COMPRESSION_ZIP = 2,        /**< ZIP compression without prediction */
    PSD_COMPRESSION_ZIP_PRED = 3,   /**< ZIP with prediction */
} psd_compression_t;


typedef enum {
    PSD_LAYER_TYPE_GROUP_END = 0,
    PSD_LAYER_TYPE_GROUP_START = 1,
    PSD_LAYER_TYPE_PIXEL = 2,
    PSD_LAYER_TYPE_TEXT = 3,
    PSD_LAYER_TYPE_SMART_OBJECT = 4,
    PSD_LAYER_TYPE_ADJUSTMENT = 5,
    PSD_LAYER_TYPE_FILL = 6,
    PSD_LAYER_TYPE_EFFECTS = 7,
    PSD_LAYER_TYPE_3D = 8,
    PSD_LAYER_TYPE_VIDEO = 9,
    PSD_LAYER_TYPE_EMPTY = 10,
} psd_layer_type_t;

/**
 * @brief Layer features detected from Additional Layer Information
 *
 * Indicates which features/properties are present in a layer.
 * A single layer may have multiple features set simultaneously.
 */
typedef struct {
    bool is_group_start;        /**< Layer is a group/folder opening */
    bool is_group_end;          /**< Layer marks the end of a group */
    bool has_text;              /**< Layer has text content (TySh) */
    bool has_vector_mask;       /**< Layer has vector mask data (vmsk/vmns) */
    bool has_smart_object;      /**< Layer is a smart object (SoLd/SoLE) */
    bool has_adjustment;        /**< Layer is an adjustment layer (adj*) */
    bool has_fill;              /**< Layer is a fill layer (SoCo, GdFl, PtFl) */
    bool has_effects;           /**< Layer has effects (lfx2) */
    bool has_3d;                /**< Layer is 3D (3dLr) */
    bool has_video;             /**< Layer is video (VsLy/vtrk) */
} psd_layer_features_t;



/**
 * @brief Allocator interface for memory management
 *
 * Allows users to provide custom memory allocation functions.
 * All functions must have the same semantics as malloc/realloc/free.
 */
typedef struct {
    /**
     * @brief Allocate memory
     * @param size Number of bytes to allocate
     * @param user_data User-defined context
     * @return Pointer to allocated memory, or NULL on failure
     */
    void* (*malloc)(size_t size, void *user_data);

    /**
     * @brief Reallocate memory
     * @param ptr Previous allocation (may be NULL)
     * @param size New size in bytes
     * @param user_data User-defined context
     * @return Pointer to reallocated memory, or NULL on failure
     */
    void* (*realloc)(void *ptr, size_t size, void *user_data);

    /**
     * @brief Free memory
     * @param ptr Previous allocation (may be NULL)
     * @param user_data User-defined context
     */
    void (*free)(void *ptr, void *user_data);

    /**
     * @brief User-defined context passed to allocation functions
     */
    void *user_data;
} psd_allocator_t;

/**
 * @brief Stream interface for reading/writing data
 *
 * Abstract interface for I/O operations. Allows reading from memory buffers,
 * files, or any other source without using FILE* pointers.
 *
 * All offsets are absolute positions from the start of the stream.
 */
typedef struct psd_stream psd_stream_t;

#endif /* PSD_TYPES_H */
