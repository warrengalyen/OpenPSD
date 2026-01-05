/**
 * @file psd_text_layer.h
 * @brief Internal text layer structures
 *
 * Defines the in-memory representation of text layer records and text layer information.
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

 #ifndef PSD_TEXT_LAYER_H
 #define PSD_TEXT_LAYER_H

 #include <stdint.h>
 #include <stddef.h>
 #include <stdbool.h>
 #include "../include/openpsd/psd_export.h"
 #include "psd_descriptor.h"

 #ifdef __cplusplus
 extern "C" {
 #endif

 /* Forward declarations */
 typedef struct psd_document psd_document_t;

 /* 2D affine transform used by TySh: xx, xy, yx, yy, tx, ty (doubles) */
 typedef struct {
     double xx, xy;
     double yx, yy;
     double tx, ty;
} psd_text_transform_t;

/* TySh stores a rectangle as 4 doubles: left, top, right, bottom */
typedef struct {
    double left;
    double top;
    double right;
    double bottom;
} psd_text_rect_t;

typedef enum {
    PSD_TEXT_SOURCE_UNKNOWN = 0,
    PSD_TEXT_SOURCE_TYSH,       /* Photoshop 6+ Type Tool Object Setting ('TySh') */
    PSD_TEXT_SOURCE_TYSH_LEGACY /* Photoshop 5/5.5 Type Tool Info ('tySh') */
} psd_text_source_t;


/* Single text layer record (derived from a layer's additional info blocks) */
typedef struct psd_text_layer {
    uint32_t layer_index;
    psd_text_source_t source;

    /* From 'TySh' (Photoshop 6+) */
    uint16_t tysh_version;         /* = 1 in spec */
    uint16_t text_version;         /* = 50 in spec */
    uint32_t text_desc_version;    /* = 16 in spec */
    uint16_t warp_version;         /* = 1 in spec */
    uint32_t warp_desc_version;    /* = 16 in spec */

    psd_text_transform_t transform;
    psd_text_rect_t      text_bounds; /* left/top/right/bottom doubles */

     /* Parsed descriptors */
    psd_descriptor_t *text_data;   /* "Text data" descriptor */
    psd_descriptor_t *warp_data;   /* "Warp data" descriptor */

    /*
      Optional: raw payload snapshots for debugging/round-tripping.
      Store exactly the TySh data block bytes (excluding signature/key/len)
      if you want lossless export later.
    */
    uint8_t  *raw_tysh;
    uint64_t  raw_tysh_len;

    /*
      Optional: Some PSDs also carry separate "text engine data" blocks
      (often discussed as 'Txt2' in the wild). Keep raw bytes if present,
      even if you don't parse them yet. :contentReference[oaicite:1]{index=1}
    */
    uint8_t  *raw_text_engine;
    uint64_t  raw_text_engine_len;

    /* Convenience flags */
    bool has_rendered_pixels; /* true if the layer has normal channels/bounds */
} psd_text_layer_t;

/* Container owned by the context */
typedef struct psd_text_layer_info {
    psd_text_layer_t *items;
    size_t count;
} psd_text_layer_info_t;


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PSD_TEXT_LAYER_H */