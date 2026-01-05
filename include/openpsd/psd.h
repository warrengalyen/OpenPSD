/**
 * @file psd.h
 * @brief Main public API for library
 *
 * This is the main header file for the library. It includes all
 * necessary headers and provides the main parser interface.
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

#ifndef PSD_H
#define PSD_H

#include "psd_export.h"
#include "psd_error.h"
#include "psd_types.h"
#include "psd_stream.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Version information
 */
#define PSD_VERSION_MAJOR 0
#define PSD_VERSION_MINOR 1
#define PSD_VERSION_PATCH 0

/**
 * @brief Get library version as semantic version string
 *
 * Returns a string like "0.1.0" representing the semantic version.
 * The returned string is a static constant and should not be freed.
 *
 * @return Version string in format "MAJOR.MINOR.PATCH"
 */
PSD_API const char* psd_get_version(void);

/**
 * @brief Get library version components
 *
 * Retrieves individual version components.
 *
 * @param major Where to store major version (can be NULL)
 * @param minor Where to store minor version (can be NULL)
 * @param patch Where to store patch version (can be NULL)
 */
PSD_API void psd_version_components(
    int *major,
    int *minor,
    int *patch
);

/**
 * @brief Opaque document handle
 *
 * Represents a parsed PSD document. All state is encapsulated in this
 * structure - there is no global state in the library.
 */
typedef struct psd_document psd_document_t;

/**
 * @brief Text justification
 */
typedef enum {
    PSD_TEXT_JUSTIFY_LEFT = 0,
    PSD_TEXT_JUSTIFY_RIGHT = 1,
    PSD_TEXT_JUSTIFY_CENTER = 2,
    PSD_TEXT_JUSTIFY_FULL = 3,
} psd_text_justification_t;

/**
 * @brief 2D affine text transform matrix (xx, xy, yx, yy, tx, ty)
 */
typedef struct {
    double xx, xy;
    double yx, yy;
    double tx, ty;
} psd_text_matrix_t;

/**
 * @brief Text bounds rectangle stored as doubles
 */
typedef struct {
    double left;
    double top;
    double right;
    double bottom;
} psd_text_bounds_t;

/**
 * @brief Single-style text layer rendering parameters
 *
 * All text layers are treated as a single style run.
 * Advanced features (warp/stroke/multi-run/paragraph composer) are ignored.
 */
#define PSD_TEXT_FONT_NAME_MAX 128
typedef struct {
    char font_name[PSD_TEXT_FONT_NAME_MAX];     /* UTF-8 font name (PostScript or family name) */
    double size;                                /* Font size in points */
    uint8_t color_rgba[4];                      /* RGBA color (0-255). Alpha is 255 if unknown */
    double tracking;                            /* Uniform adjustment of space between characters */
    double leading;                             /* Vertical space between lines of text (may be 0 if unknown) */
    psd_text_justification_t justification;      /* Paragraph justification */
} psd_text_style_t;

/**
 * @brief Parse a PSD file from a stream
 *
 * Parses a PSD file from the given stream. The stream must be positioned
 * at the start of the file. After parsing, the stream can be destroyed
 * independently of the document.
 *
 * @param stream Stream to read from (required)
 * @param allocator Custom memory allocator (NULL for default)
 * @return Parsed document on success, NULL on failure
 */
PSD_API psd_document_t* psd_parse(
    psd_stream_t *stream,
    const psd_allocator_t *allocator
);

/**
 * @brief Parse a PSD/PSB file from a stream (extended)
 *
 * Same as psd_parse(), but also returns a status code describing the failure
 * reason when parsing fails.
 *
 * @param stream Stream to read from (required)
 * @param allocator Custom memory allocator (NULL for default)
 * @param out_status Where to store status (can be NULL)
 * @return Parsed document on success, NULL on failure
 */
PSD_API psd_document_t* psd_parse_ex(
    psd_stream_t *stream,
    const psd_allocator_t *allocator,
    psd_status_t *out_status
);

/**
 * @brief Free a parsed document
 *
 * Frees all resources associated with the document.
 * Safe to call with NULL.
 *
 * @param doc Document to free
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_free(psd_document_t *doc);

/**
 * @brief Get document dimensions
 *
 * @param doc Document to query (required)
 * @param width Where to store width (can be NULL)
 * @param height Where to store height (can be NULL)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_get_dimensions(
    const psd_document_t *doc,
    uint32_t *width,
    uint32_t *height
);

/**
 * @brief Get document color mode
 *
 * @param doc Document to query (required)
 * @param color_mode Where to store color mode (required)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_get_color_mode(
    const psd_document_t *doc,
    psd_color_mode_t *color_mode
);

/**
 * @brief Get document bit depth
 *
 * @param doc Document to query (required)
 * @param depth Where to store bit depth (1, 8, 16, or 32) (required)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_get_depth(
    const psd_document_t *doc,
    uint16_t *depth
);

/**
 * @brief Get number of color channels
 *
 * @param doc Document to query (required)
 * @param channels Where to store channel count (required)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_get_channels(
    const psd_document_t *doc,
    uint16_t *channels
);

/**
 * @brief Check if document is in PSB (Large Document) format
 *
 * PSB format supports larger dimensions and resources than standard PSD.
 *
 * @param doc Document to query (required)
 * @param is_psb Where to store PSB flag (true=PSB, false=PSD) (required)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_is_psb(
    const psd_document_t *doc,
    bool *is_psb
);

/**
 * @brief Get raw color mode data
 *
 * Returns the raw bytes of the Color Mode Data section.
 * For indexed color: contains RGB palette (256 * 3 bytes)
 * For duotone: contains duotone information
 * For other modes: usually empty but may contain additional data
 *
 * The returned pointer is valid only while the document exists.
 * The data is NOT interpreted - it's stored as-is from the file.
 *
 * @param doc Document to query (required)
 * @param data Where to store pointer to data (can be NULL if length is 0)
 * @param length Where to store length of data (can be NULL)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_get_color_mode_data(
    const psd_document_t *doc,
    const uint8_t **data,
    uint64_t *length
);

/**
 * @brief Get number of image resources
 *
 * Returns the count of resource blocks in the Image Resources section.
 *
 * @param doc Document to query (required)
 * @param count Where to store the count (required)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_get_resource_count(
    const psd_document_t *doc,
    size_t *count
);

/**
 * @brief Get information about a resource block
 *
 * Retrieves information about a specific resource block by index.
 * All returned pointers are valid only while the document exists.
 *
 * @param doc Document to query (required)
 * @param index Index of resource (0-based)
 * @param id Where to store resource ID (required)
 * @param data Where to store pointer to resource data (can be NULL)
 * @param length Where to store length of resource data (can be NULL)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_get_resource(
    const psd_document_t *doc,
    size_t index,
    uint16_t *id,
    const uint8_t **data,
    uint64_t *length
);

/**
 * @brief Find a resource by ID
 *
 * Searches for the first resource with the given ID.
 * Returns the index if found, or indicates not found via return code.
 *
 * @param doc Document to query (required)
 * @param id Resource ID to find
 * @param index Where to store the index if found (required)
 * @return PSD_OK if found, PSD_ERR_INVALID_ARGUMENT if not found
 */
PSD_API psd_status_t psd_document_find_resource(
    const psd_document_t *doc,
    uint16_t id,
    size_t *index
);

/**
 * @brief Get number of layers in document
 *
 * @param doc Document to query (required)
 * @param count Where to store the layer count (required)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_get_layer_count(
    const psd_document_t *doc,
    int32_t *count
);

/**
 * @brief Check if document has transparency layer
 *
 * Some PSD files have a transparency layer (indicated by negative layer count).
 *
 * @param doc Document to query (required)
 * @param has_transparency Where to store result (required)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_has_transparency_layer(
    const psd_document_t *doc,
    bool *has_transparency
);

/**
 * @brief Get layer bounding box
 *
 * @param doc Document to query (required)
 * @param index Layer index (0-based)
 * @param top Where to store top coordinate (can be NULL)
 * @param left Where to store left coordinate (can be NULL)
 * @param bottom Where to store bottom coordinate (can be NULL)
 * @param right Where to store right coordinate (can be NULL)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_get_layer_bounds(
    const psd_document_t *doc,
    int32_t index,
    int32_t *top,
    int32_t *left,
    int32_t *bottom,
    int32_t *right
);

/**
 * @brief Get layer blend mode
 *
 * @param doc Document to query (required)
 * @param index Layer index (0-based)
 * @param blend_sig Where to store blend mode signature (required)
 * @param blend_key Where to store blend mode key (required)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_get_layer_blend_mode(
    const psd_document_t *doc,
    int32_t index,
    uint32_t *blend_sig,
    uint32_t *blend_key
);

/**
 * @brief Get layer opacity and properties
 *
 * @param doc Document to query (required)
 * @param index Layer index (0-based)
 * @param opacity Where to store opacity 0-255 (can be NULL)
 * @param flags Where to store layer flags (can be NULL)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_get_layer_properties(
    const psd_document_t *doc,
    int32_t index,
    uint8_t *opacity,
    uint8_t *flags
);

/**
 * @brief Get number of channels in a layer
 *
 * @param doc Document to query (required)
 * @param layer_index Layer index (0-based)
 * @param count Where to store channel count (required)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_get_layer_channel_count(
    const psd_document_t *doc,
    int32_t layer_index,
    size_t *count
);

/**
 * @brief Get layer name
 *
 * Returns the layer name as a UTF-8 string. The name may contain null bytes
 * and should be interpreted as the specified length. The returned pointer
 * is valid only while the document exists.
 *
 * @param doc Document to query (required)
 * @param layer_index Layer index (0-based)
 * @param name Where to store pointer to name data (can be NULL if no name)
 * @param name_length Where to store length of name in bytes (can be NULL)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_get_layer_name(
    const psd_document_t *doc,
    int32_t layer_index,
    const uint8_t **name,
    size_t *name_length
);

/**
 * @brief Get layer features
 *
 * Returns the features detected from the layer's Additional Layer Information blocks.
 * A single layer may have multiple features set simultaneously.
 *
 * @param doc Document to query (required)
 * @param layer_index Layer index (0-based)
 * @param features Where to store layer features (required)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_get_layer_features(
    const psd_document_t *doc,
    int32_t layer_index,
    psd_layer_features_t *features
);

/**
 * @brief Get layer type
 *
 * Returns the layer type as an enum value, determined from the layer's features.
 * This function provides a simplified classification of layer types based on
 * the detected features from Additional Layer Information blocks.
 *
 * @param doc Document to query (required)
 * @param layer_index Layer index (0-based)
 * @param type Where to store the layer type (required)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_get_layer_type(
    const psd_document_t *doc,
    int32_t layer_index,
    psd_layer_type_t *type
);

/**
 * @brief Check if a layer is a Photoshop Background layer
 *
 * A true background layer must meet ALL of these criteria:
 * 1. It is the bottom-most layer (index == layer_count - 1)
 * 2. It has the background flag set (bit 2 in flags byte)
 * 3. It has no transparency (no alpha/transparency channel with ID = -1)
 * 4. It has no layer mask data
 * 5. It has no vector mask (vmsk/vmns keys not present)
 * 6. Its channel count equals base_channel_count (RGB=3, CMYK=4, Grayscale=1, etc.)
 *
 * This function detects the actual Photoshop background layer, not just any
 * layer named "Background" or with special properties. There can be at most
 * one background layer per document.
 *
 * @param doc Document to query (required)
 * @param layer_index Layer index (0-based)
 * @param base_channel_count Number of base color channels (RGB=3, CMYK=4, etc.)
 *                            Typically obtained from document color mode
 * @return PSD_TRUE if layer is a background layer, PSD_FALSE otherwise
 */
PSD_API psd_bool_t psd_document_is_background_layer(
    const psd_document_t *doc,
    int32_t layer_index,
    int base_channel_count
);

/**
 * @brief Get composite image data
 *
 * Returns the final composite (flattened) image. The data is organized
 * in planar format: all scanlines of channel 0, then channel 1, etc.
 *
 * @param doc Document to query (required)
 * @param data Where to store pointer to image data (can be NULL if no composite)
 * @param length Where to store data length in bytes (can be NULL)
 * @param compression Where to store compression type (can be NULL)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_get_composite_image(
    const psd_document_t *doc,
    const uint8_t **data,
    uint64_t *length,
    uint32_t *compression
);

/**
 * @brief Extra info about composite rendering
 */
typedef struct {
    psd_color_mode_t color_mode;      /**< Document color mode */
    uint16_t depth_bits;             /**< Bits per channel */
    uint16_t channels;               /**< Document channel count */
    uint32_t compression;            /**< Composite compression (0..3) */
} psd_render_composite_info_t;

/**
 * @brief Render composite image to RGBA8
 *
 * Converts the document's composite (flattened) image from the PSD's native
 * color mode (RGB/Grayscale/Indexed/CMYK/Lab/Bitmap) into an interleaved
 * 8-bit RGBA buffer (non-premultiplied).
 *
 * This is the recommended way to display composites from non-RGB documents.
 *
 * @param doc Document to query (required)
 * @param out_rgba Output buffer (may be NULL to query required size)
 * @param out_rgba_size Size of output buffer in bytes
 * @param out_required_size Where to store required size in bytes (can be NULL)
 * @return PSD_OK on success, PSD_ERR_BUFFER_TOO_SMALL if buffer is too small,
 *         PSD_ERR_UNSUPPORTED_COLOR_MODE if conversion not supported, or other error.
 */
PSD_API psd_status_t psd_document_render_composite_rgba8(
    const psd_document_t *doc,
    uint8_t *out_rgba,
    size_t out_rgba_size,
    size_t *out_required_size
);

/**
 * @brief Render composite image to RGBA8 (extended)
 *
 * Same as psd_document_render_composite_rgba8(), but also returns the composite
 * compression mode and basic document properties in out_info.
 *
 * @param doc Document to query (required)
 * @param out_rgba Output buffer (may be NULL to query required size)
 * @param out_rgba_size Size of output buffer in bytes
 * @param out_required_size Where to store required size in bytes (can be NULL)
 * @param out_info Optional output info (can be NULL)
 */
PSD_API psd_status_t psd_document_render_composite_rgba8_ex(
    const psd_document_t *doc,
    uint8_t *out_rgba,
    size_t out_rgba_size,
    size_t *out_required_size,
    psd_render_composite_info_t *out_info
);

/**
 * @brief Render a pixel layer to RGBA8
 *
 * Renders an individual pixel layer to an interleaved 8-bit RGBA buffer
 * (non-premultiplied), converting from the PSD's native color mode.
 *
 * The output represents the layer's bounding box (not the full document).
 * Use psd_document_get_layer_bounds() to position it in the document.
 *
 * @param doc Document (required). Non-const because layer channels may be lazily decoded.
 * @param layer_index Layer index (0-based)
 * @param out_rgba Output buffer (may be NULL to query required size)
 * @param out_rgba_size Size of output buffer in bytes
 * @param out_required_size Where to store required size in bytes (can be NULL)
 * @return PSD_OK on success, PSD_ERR_BUFFER_TOO_SMALL if buffer is too small,
 *         PSD_ERR_UNSUPPORTED_COLOR_MODE if conversion not supported, or other error.
 */
PSD_API psd_status_t psd_document_render_layer_rgba8(
    psd_document_t *doc,
    int32_t layer_index,
    uint8_t *out_rgba,
    size_t out_rgba_size,
    size_t *out_required_size
);

/**
 * @brief Get layer channel info and decode on demand
 *
 * Returns information about a layer channel with lazy decoding support.
 * The channel data is decompressed only when this function is called.
 * Supports RAW and RLE formats. ZIP formats are returned as compressed.
 *
 * @param doc Document to query (required)
 * @param layer_index Layer index (0-based)
 * @param channel_index Channel index within layer (0-based)
 * @param channel_id Where to store channel ID (can be NULL)
 * @param data Where to store decoded pixel data pointer (can be NULL)
 * @param length Where to store decoded data length in bytes (can be NULL)
 * @param compression Where to store original compression type (can be NULL)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_get_layer_channel_data(
    psd_document_t *doc,
    int32_t layer_index,
    size_t channel_index,
    int16_t *channel_id,
    const uint8_t **data,
    uint64_t *length,
    uint32_t *compression
);

/**
 * @brief Get layer raw descriptor data
 *
 * Returns the raw binary descriptor data for a layer (layer effects, text, etc).
 * The descriptor structure is preserved as raw bytes for forward compatibility.
 * Interpretation is left to the application.
 *
 * @param doc Document to query (required)
 * @param layer_index Layer index (0-based)
 * @param descriptor_data Where to store raw descriptor bytes (can be NULL if no descriptor)
 * @param descriptor_length Where to store descriptor data length (can be NULL)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_document_get_layer_descriptor(
    const psd_document_t *doc,
    int32_t layer_index,
    const uint8_t **descriptor_data,
    uint64_t *descriptor_length
);

/**
 * @brief Extract text content from a text layer
 *
 * Attempts to extract the actual text string from a text layer's descriptor data.
 * This requires parsing the TySh descriptor which is expensive, so it's done on-demand.
 *
 * @param doc Document containing the text layer (required)
 * @param layer_index Layer index (0-based)
 * @param buffer Buffer to store extracted text
 * @param buffer_size Size of buffer in bytes
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_text_layer_get_text(
    psd_document_t *doc,
    uint32_t layer_index,
    char *buffer,
    size_t buffer_size
);

/**
 * @brief Get the default (single-run) text style for a text layer
 *
 * Extraction of font name, size, color, tracking, leading, and justification.
 * Treats the text layer as a single style run.
 *
 * @param doc Document containing the text layer (required)
 * @param layer_index Layer index (0-based)
 * @param out_style Output style structure (required)
 * @return PSD_OK on success, or error on failure / unsupported structure
 */
PSD_API psd_status_t psd_text_layer_get_default_style(
    psd_document_t *doc,
    uint32_t layer_index,
    psd_text_style_t *out_style
);

/**
 * @brief Get text transform matrix and bounds for rendering
 *
 * @param doc Document containing the text layer (required)
 * @param layer_index Layer index (0-based)
 * @param out_matrix Output transform matrix (required)
 * @param out_bounds Output bounds (required)
 * @return PSD_OK on success or error on failure
 */
PSD_API psd_status_t psd_text_layer_get_matrix_bounds(
    psd_document_t *doc,
    uint32_t layer_index,
    psd_text_matrix_t *out_matrix,
    psd_text_bounds_t *out_bounds
);

#ifdef __cplusplus
}
#endif

#endif /* PSD_H */
