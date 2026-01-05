/**
 * @file test_basic.c
 * @brief Basic functionality tests
 *
 * Simple test program to verify the library builds and links correctly.
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

#include <openpsd/psd.h>
#include "openpsd_tests.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#ifndef OPENPSD_TEST_SAMPLES_DIR
#define OPENPSD_TEST_SAMPLES_DIR "tests/samples"
#endif

static void join_path(char *out, size_t out_cap, const char *dir, const char *file)
{
    if (!out || out_cap == 0) return;
    out[0] = '\0';
    if (!dir) dir = "";
    if (!file) file = "";
    /* Forward slashes work fine on Windows fopen(). */
    (void)snprintf(out, out_cap, "%s/%s", dir, file);
}

static const char *color_mode_name(psd_color_mode_t m)
{
    switch (m) {
    case PSD_COLOR_BITMAP: return "Bitmap";
    case PSD_COLOR_GRAYSCALE: return "Grayscale";
    case PSD_COLOR_INDEXED: return "Indexed";
    case PSD_COLOR_RGB: return "RGB";
    case PSD_COLOR_CMYK: return "CMYK";
    case PSD_COLOR_MULTICHANNEL: return "Multichannel";
    case PSD_COLOR_DUOTONE: return "Duotone";
    case PSD_COLOR_LAB: return "Lab";
    default: return "Unknown";
    }
}

/**
 * @brief Test version functions
 */
static int test_version(void)
{
    printf("Testing version functions...\n");

    const char *version = psd_get_version();
    printf("  Version string: %s\n", version);

    int major, minor, patch;
    psd_version_components(&major, &minor, &patch);
    printf("  Version components: %d.%d.%d\n", major, minor, patch);

    if (strcmp(version, "0.1.0") != 0) {
        fprintf(stderr, "ERROR: Version string mismatch\n");
        return 1;
    }

    if (major != 0 || minor != 1 || patch != 0) {
        fprintf(stderr, "ERROR: Version components mismatch\n");
        return 1;
    }

    printf("  OK\n\n");
    return 0;
}

/**
 * @brief Test error strings
 */
static int test_error_strings(void)
{
    printf("Testing error string functions...\n");

    psd_status_t codes[] = {
        PSD_OK,
        PSD_ERR_INVALID_ARGUMENT,
        PSD_ERR_OUT_OF_MEMORY,
        PSD_ERR_INVALID_FILE_FORMAT,
        PSD_ERR_STREAM_EOF,
    };

    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        const char *msg = psd_error_string(codes[i]);
        printf("  Error code %d: %s\n", codes[i], msg);
        if (!msg || strlen(msg) == 0) {
            fprintf(stderr, "ERROR: Invalid error message\n");
            return 1;
        }
    }

    printf("  OK\n\n");
    return 0;
}

/**
 * @brief Test stream creation and basic operations
 */
static int test_stream(void)
{
    printf("Testing stream functions...\n");

    /* Create a simple test buffer */
    uint8_t buffer[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    size_t buffer_size = sizeof(buffer);

    /* Create stream from buffer */
    psd_stream_t *stream = psd_stream_create_buffer(NULL, buffer, buffer_size);
    if (!stream) {
        fprintf(stderr, "ERROR: Failed to create stream\n");
        return 1;
    }
    printf("  Stream created OK\n");

    /* Test tell at start */
    int64_t pos = psd_stream_tell(stream);
    if (pos != 0) {
        fprintf(stderr, "ERROR: Initial position should be 0, got %ld\n", (long)pos);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  Initial position: %ld\n", (long)pos);

    /* Test read */
    uint8_t read_buf[4];
    int64_t read_count = psd_stream_read(stream, read_buf, 4);
    if (read_count != 4) {
        fprintf(stderr, "ERROR: Should have read 4 bytes, got %ld\n", (long)read_count);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  Read 4 bytes OK\n");

    /* Test position after read */
    pos = psd_stream_tell(stream);
    if (pos != 4) {
        fprintf(stderr, "ERROR: Position should be 4 after read, got %ld\n", (long)pos);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  Position after read: %ld\n", (long)pos);

    /* Test seek */
    int64_t new_pos = psd_stream_seek(stream, 0);
    if (new_pos != 0) {
        fprintf(stderr, "ERROR: Seek should return 0, got %ld\n", (long)new_pos);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  Seek to 0 OK\n");

    /* Test big-endian read */
    uint16_t be16_val;
    psd_status_t status = psd_stream_read_be16(stream, &be16_val);
    if (status != PSD_OK) {
        fprintf(stderr, "ERROR: psd_stream_read_be16 failed: %s\n", psd_error_string(status));
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  Read big-endian 16-bit: 0x%04x\n", be16_val);

    /* Test skip */
    status = psd_stream_skip(stream, 2);
    if (status != PSD_OK) {
        fprintf(stderr, "ERROR: psd_stream_skip failed: %s\n", psd_error_string(status));
        psd_stream_destroy(stream);
        return 1;
    }
    pos = psd_stream_tell(stream);
    if (pos != 4) {
        fprintf(stderr, "ERROR: Position should be 4 after skip, got %ld\n", (long)pos);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  Skip 2 bytes OK, position: %ld\n", (long)pos);

    /* Cleanup */
    status = psd_stream_destroy(stream);
    if (status != PSD_OK) {
        fprintf(stderr, "ERROR: psd_stream_destroy failed\n");
        return 1;
    }
    printf("  Stream destroyed OK\n\n");

    return 0;
}

/**
 * @brief Test allocator functions
 */
static int test_allocator(void)
{
    printf("Testing allocator functions...\n");

    /* Test default allocator */
    const psd_allocator_t *alloc = NULL;

    /* Create stream with NULL allocator (should use default) */
    uint8_t buffer[] = {0x00, 0x01, 0x02, 0x03};
    psd_stream_t *stream = psd_stream_create_buffer(alloc, buffer, sizeof(buffer));
    if (!stream) {
        fprintf(stderr, "ERROR: Failed to create stream with default allocator\n");
        return 1;
    }
    printf("  Stream with default allocator OK\n");

    psd_stream_destroy(stream);
    printf("  OK\n\n");

    return 0;
}

/**
 * @brief Test endian-safe primitive readers
 */
static int test_endian_readers(void)
{
    printf("Testing endian-safe primitive readers...\n");

    /* Test data: BE16=0x0102, BE32=0x01020304, BEi32=0xFF020304 (-16656124), BE64=0x0102030405060708 */
    uint8_t buffer[] = {
        0x01, 0x02,                                   /* BE16 */
        0x01, 0x02, 0x03, 0x04,                       /* BE32 */
        0xFF, 0x02, 0x03, 0x04,                       /* BEi32 */
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08  /* BE64 */
    };

    psd_stream_t *stream = psd_stream_create_buffer(NULL, buffer, sizeof(buffer));
    if (!stream) {
        fprintf(stderr, "ERROR: Failed to create stream\n");
        return 1;
    }

    /* Read unsigned 16-bit */
    uint16_t u16;
    psd_status_t status = psd_stream_read_be16(stream, &u16);
    if (status != PSD_OK || u16 != 0x0102) {
        fprintf(stderr, "ERROR: psd_stream_read_be16 failed\n");
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  Read BE16: 0x%04x OK\n", u16);

    /* Read unsigned 32-bit */
    uint32_t u32;
    status = psd_stream_read_be32(stream, &u32);
    if (status != PSD_OK || u32 != 0x01020304) {
        fprintf(stderr, "ERROR: psd_stream_read_be32 failed\n");
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  Read BE32: 0x%08x OK\n", u32);

    /* Read signed 32-bit */
    int32_t i32;
    status = psd_stream_read_be_i32(stream, &i32);
    if (status != PSD_OK) {
        fprintf(stderr, "ERROR: psd_stream_read_be_i32 failed\n");
        psd_stream_destroy(stream);
        return 1;
    }
    if (i32 != (int32_t)0xFF020304) {
        fprintf(stderr, "ERROR: psd_stream_read_be_i32 value mismatch: got 0x%08x\n", (uint32_t)i32);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  Read BEi32: 0x%08x OK\n", (uint32_t)i32);

    /* Read unsigned 64-bit */
    uint64_t u64;
    status = psd_stream_read_be64(stream, &u64);
    if (status != PSD_OK || u64 != 0x0102030405060708ULL) {
        fprintf(stderr, "ERROR: psd_stream_read_be64 failed\n");
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  Read BE64: 0x%016llx OK\n", (unsigned long long)u64);

    psd_stream_destroy(stream);
    printf("  OK\n\n");

    return 0;
}

/**
 * @brief Test length reader with overflow detection
 */
static int test_length_reader(void)
{
    printf("Testing length reader with overflow detection...\n");

    /* Test data: 32-bit length (0x00001000 = 4096), 64-bit length (0x0000000000002000 = 8192) */
    uint8_t buffer[] = {
        0x00, 0x00, 0x10, 0x00,                       /* 32-bit: 4096 */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00  /* 64-bit: 8192 */
    };

    psd_stream_t *stream = psd_stream_create_buffer(NULL, buffer, sizeof(buffer));
    if (!stream) {
        fprintf(stderr, "ERROR: Failed to create stream\n");
        return 1;
    }

    /* Read 32-bit length (standard PSD) */
    uint64_t len32;
    psd_status_t status = psd_stream_read_length(stream, false, &len32);
    if (status != PSD_OK || len32 != 4096) {
        fprintf(stderr, "ERROR: psd_stream_read_length(32-bit) failed or value mismatch\n");
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  Read 32-bit length: %llu OK\n", (unsigned long long)len32);

    /* Read 64-bit length (PSB format) */
    uint64_t len64;
    status = psd_stream_read_length(stream, true, &len64);
    if (status != PSD_OK || len64 != 8192) {
        fprintf(stderr, "ERROR: psd_stream_read_length(64-bit) failed or value mismatch\n");
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  Read 64-bit length: %llu OK\n", (unsigned long long)len64);

    psd_stream_destroy(stream);
    printf("  OK\n\n");

    return 0;
}

/**
 * @brief Test PSD/PSB header parsing
 */
static int test_psd_header_parsing(void)
{
    printf("Testing PSD/PSB header parsing...\n");

    /* Test data for standard PSD (v1) header:
     * Signature: "8BPS" (0x38425053)
     * Version: 1 (0x0001)
     * Reserved: 6 bytes (0x000000000000)
     * Channels: 3 (0x0003) - RGB
     * Height: 256 (0x00000100)
     * Width: 512 (0x00000200)
     * Depth: 8 (0x0008)
     * Color Mode: RGB=3 (0x0003)
     * Color Data Length: 0 (0x00000000) - minimal header
     */
    uint8_t psd_header[] = {
        0x38, 0x42, 0x50, 0x53,     /* Signature "8BPS" */
        0x00, 0x01,                 /* Version 1 (PSD) */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* Reserved: 6 bytes */
        0x00, 0x03,                 /* 3 channels */
        0x00, 0x00, 0x01, 0x00,     /* Height: 256 */
        0x00, 0x00, 0x02, 0x00,     /* Width: 512 */
        0x00, 0x08,                 /* Depth: 8 bits */
        0x00, 0x03,                 /* Color mode: RGB */
        0x00, 0x00, 0x00, 0x00,     /* Color data length: 0 */
        0x00, 0x00, 0x00, 0x00,     /* Resources length: 0 */
        0x00, 0x00, 0x00, 0x00      /* Layer/Mask info length: 0 */
    };

    psd_stream_t *stream = psd_stream_create_buffer(NULL, psd_header, sizeof(psd_header));
    if (!stream) {
        fprintf(stderr, "ERROR: Failed to create stream for PSD header test\n");
        return 1;
    }

    psd_status_t parse_status = PSD_OK;
    psd_document_t *doc = psd_parse_ex(stream, NULL, &parse_status);
    if (!doc) {
        fprintf(stderr, "ERROR: Failed to parse PSD header (status=%d: %s)\n",
                (int)parse_status, psd_error_string(parse_status));
        psd_stream_destroy(stream);
        return 1;
    }

    /* Verify dimensions */
    uint32_t width, height;
    psd_document_get_dimensions(doc, &width, &height);
    if (width != 512 || height != 256) {
        fprintf(stderr, "ERROR: Dimension mismatch: got %ux%u, expected 512x256\n", width, height);
        psd_document_free(doc);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  PSD dimensions: %ux%u OK\n", width, height);

    /* Verify depth */
    uint16_t depth;
    psd_document_get_depth(doc, &depth);
    if (depth != 8) {
        fprintf(stderr, "ERROR: Depth mismatch: got %u, expected 8\n", depth);
        psd_document_free(doc);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  PSD depth: %u bits OK\n", depth);

    /* Verify channels */
    uint16_t channels;
    psd_document_get_channels(doc, &channels);
    if (channels != 3) {
        fprintf(stderr, "ERROR: Channels mismatch: got %u, expected 3\n", channels);
        psd_document_free(doc);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  PSD channels: %u OK\n", channels);

    /* Verify it's not PSB */
    bool is_psb;
    psd_document_is_psb(doc, &is_psb);
    if (is_psb) {
        fprintf(stderr, "ERROR: Should not be PSB format\n");
        psd_document_free(doc);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  PSD format (not PSB) OK\n");

    /* Verify color mode */
    psd_color_mode_t color_mode;
    psd_document_get_color_mode(doc, &color_mode);
    if (color_mode != PSD_COLOR_RGB) {
        fprintf(stderr, "ERROR: Color mode mismatch: got %u, expected %u (RGB)\n", color_mode, PSD_COLOR_RGB);
        psd_document_free(doc);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  PSD color mode: RGB OK\n");

    psd_document_free(doc);
    psd_stream_destroy(stream);

    /* Test PSB (v2) header */
    uint8_t psb_header[] = {
        0x38, 0x42, 0x50, 0x53,     /* Signature "8BPS" */
        0x00, 0x02,                 /* Version 2 (PSB) */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* Reserved: 6 bytes */
        0x00, 0x04,                 /* 4 channels (CMYK) */
        0x00, 0x00, 0x50, 0x00,     /* Height: 20480 */
        0x00, 0x00, 0xA0, 0x00,     /* Width: 40960 */
        0x00, 0x10,                 /* Depth: 16 bits */
        0x00, 0x04,                 /* Color mode: CMYK */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* Color data length: 0 (8 bytes for PSB) */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* Resources length: 0 (8 bytes for PSB) */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00   /* Layer/Mask info length: 0 (8 bytes for PSB) */
    };

    stream = psd_stream_create_buffer(NULL, psb_header, sizeof(psb_header));
    if (!stream) {
        fprintf(stderr, "ERROR: Failed to create stream for PSB header test\n");
        return 1;
    }

    parse_status = PSD_OK;
    doc = psd_parse_ex(stream, NULL, &parse_status);
    if (!doc) {
        fprintf(stderr, "ERROR: Failed to parse PSB header (status=%d: %s)\n",
                (int)parse_status, psd_error_string(parse_status));
        psd_stream_destroy(stream);
        return 1;
    }

    psd_document_get_dimensions(doc, &width, &height);
    if (width != 40960 || height != 20480) {
        fprintf(stderr, "ERROR: PSB dimension mismatch: got %ux%u, expected 40960x20480\n", width, height);
        psd_document_free(doc);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  PSB dimensions: %ux%u OK\n", width, height);

    psd_document_get_depth(doc, &depth);
    if (depth != 16) {
        fprintf(stderr, "ERROR: PSB depth mismatch: got %u, expected 16\n", depth);
        psd_document_free(doc);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  PSB depth: %u bits OK\n", depth);

    psd_document_is_psb(doc, &is_psb);
    if (!is_psb) {
        fprintf(stderr, "ERROR: Should be PSB format\n");
        psd_document_free(doc);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  PSB format (v2) OK\n");

    psd_document_free(doc);
    psd_stream_destroy(stream);
    printf("  OK\n\n");

    return 0;
}

/**
 * @brief Test invalid headers
 */
static int test_invalid_headers(void)
{
    printf("Testing invalid header detection...\n");

    /* Test 1: Invalid signature */
    uint8_t bad_sig[] = {
        0xFF, 0xFF, 0xFF, 0xFF,     /* Invalid signature */
        0x00, 0x01, 0x00, 0x00, 0x00, 0x03
    };

    psd_stream_t *stream = psd_stream_create_buffer(NULL, bad_sig, sizeof(bad_sig));
    if (!stream) {
        fprintf(stderr, "ERROR: Failed to create stream\n");
        return 1;
    }

    psd_document_t *doc = psd_parse(stream, NULL);
    if (doc != NULL) {
        fprintf(stderr, "ERROR: Should have rejected invalid signature\n");
        psd_document_free(doc);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  Rejected invalid signature OK\n");
    psd_stream_destroy(stream);

    /* Test 2: Invalid version */
    uint8_t bad_ver[] = {
        0x38, 0x42, 0x50, 0x53,     /* Valid signature */
        0x00, 0x03,                 /* Invalid version (3) */
        0x00, 0x00, 0x00, 0x03
    };

    stream = psd_stream_create_buffer(NULL, bad_ver, sizeof(bad_ver));
    if (!stream) {
        fprintf(stderr, "ERROR: Failed to create stream\n");
        return 1;
    }

    doc = psd_parse(stream, NULL);
    if (doc != NULL) {
        fprintf(stderr, "ERROR: Should have rejected invalid version\n");
        psd_document_free(doc);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  Rejected invalid version OK\n");
    psd_stream_destroy(stream);

    /* Test 3: Invalid channels (0) */
    uint8_t bad_chan[] = {
        0x38, 0x42, 0x50, 0x53,     /* Signature */
        0x00, 0x01,                 /* Version 1 */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* Reserved: 6 bytes */
        0x00, 0x00,                 /* Invalid: 0 channels */
        0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00
    };

    stream = psd_stream_create_buffer(NULL, bad_chan, sizeof(bad_chan));
    if (!stream) {
        fprintf(stderr, "ERROR: Failed to create stream\n");
        return 1;
    }

    doc = psd_parse(stream, NULL);
    if (doc != NULL) {
        fprintf(stderr, "ERROR: Should have rejected 0 channels\n");
        psd_document_free(doc);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  Rejected invalid channel count OK\n");
    psd_stream_destroy(stream);

    printf("  OK\n\n");
    return 0;
}

/**
 * @brief Test Color Mode Data section parsing
 */
static int test_color_mode_data_parsing(void)
{
    printf("Testing Color Mode Data section parsing...\n");

    /* Test 1: No color mode data (empty) */
    uint8_t psd_no_color_data[] = {
        0x38, 0x42, 0x50, 0x53,                    /* Signature */
        0x00, 0x01,                                /* Version 1 */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,       /* Reserved: 6 bytes */
        0x00, 0x03,                                /* Channels: 3 */
        0x00, 0x00, 0x01, 0x00,                    /* Height: 256 */
        0x00, 0x00, 0x02, 0x00,                    /* Width: 512 */
        0x00, 0x08,                                /* Depth: 8 */
        0x00, 0x03,                                /* Color Mode: RGB */
        0x00, 0x00, 0x00, 0x00,                    /* Color Data Length: 0 */
        0x00, 0x00, 0x00, 0x00,                    /* Resources Length: 0 */
        0x00, 0x00, 0x00, 0x00                     /* Layer/Mask info Length: 0 */
    };

    psd_stream_t *stream = psd_stream_create_buffer(NULL, psd_no_color_data, sizeof(psd_no_color_data));
    if (!stream) {
        fprintf(stderr, "ERROR: Failed to create stream\n");
        return 1;
    }

    psd_document_t *doc = psd_parse(stream, NULL);
    if (!doc) {
        fprintf(stderr, "ERROR: Failed to parse PSD with empty color data\n");
        psd_stream_destroy(stream);
        return 1;
    }

    const uint8_t *color_data;
    uint64_t color_length;
    psd_document_get_color_mode_data(doc, &color_data, &color_length);

    if (color_length != 0) {
        fprintf(stderr, "ERROR: Expected empty color data, got %llu bytes\n", (unsigned long long)color_length);
        psd_document_free(doc);
        psd_stream_destroy(stream);
        return 1;
    }
    if (color_data != NULL) {
        fprintf(stderr, "ERROR: Expected NULL color data pointer\n");
        psd_document_free(doc);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  Empty color data OK\n");

    psd_document_free(doc);
    psd_stream_destroy(stream);

    printf("  OK\n\n");
    return 0;
}

/**
 * @brief Test Image Resources parsing with forward compatibility
 */
static int test_image_resources_parsing(void)
{
    printf("Testing Image Resources section parsing...\n");

    /* Test 1: No resources (empty section) */
    uint8_t psd_no_resources[] = {
        0x38, 0x42, 0x50, 0x53,                    /* Signature */
        0x00, 0x01,                                /* Version 1 */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,       /* Reserved: 6 bytes */
        0x00, 0x03,                                /* Channels: 3 */
        0x00, 0x00, 0x01, 0x00,                    /* Height: 256 */
        0x00, 0x00, 0x02, 0x00,                    /* Width: 512 */
        0x00, 0x08,                                /* Depth: 8 */
        0x00, 0x03,                                /* Color Mode: RGB */
        0x00, 0x00, 0x00, 0x00,                    /* Color Data Length: 0 */
        0x00, 0x00, 0x00, 0x00,                    /* Resources Length: 0 */
        0x00, 0x00, 0x00, 0x00                     /* Layer/Mask info Length: 0 */
    };

    psd_stream_t *stream = psd_stream_create_buffer(NULL, psd_no_resources, sizeof(psd_no_resources));
    if (!stream) {
        fprintf(stderr, "ERROR: Failed to create stream\n");
        return 1;
    }

    psd_document_t *doc = psd_parse(stream, NULL);
    if (!doc) {
        fprintf(stderr, "ERROR: Failed to parse PSD with empty resources\n");
        psd_stream_destroy(stream);
        return 1;
    }

    size_t resource_count;
    psd_document_get_resource_count(doc, &resource_count);
    if (resource_count != 0) {
        fprintf(stderr, "ERROR: Expected 0 resources, got %zu\n", resource_count);
        psd_document_free(doc);
        psd_stream_destroy(stream);
        return 1;
    }
    printf("  Empty resources section OK\n");

    psd_document_free(doc);
    psd_stream_destroy(stream);

    /* Test 2: Resource block with data */
    uint8_t test_resource_data[] = {0xDE, 0xAD, 0xBE, 0xEF};

    /* Build PSD with one resource block */
    /* Resource block: sig(4) + id(2) + name_len(2) + name(0) + data_len(4) + data(4) = 16 bytes */
    uint8_t psd_with_resource[] = {
        0x38, 0x42, 0x50, 0x53,                    /* Signature */
        0x00, 0x01,                                /* Version 1 */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,       /* Reserved: 6 bytes */
        0x00, 0x03,                                /* Channels: 3 */
        0x00, 0x00, 0x01, 0x00,                    /* Height: 256 */
        0x00, 0x00, 0x02, 0x00,                    /* Width: 512 */
        0x00, 0x08,                                /* Depth: 8 */
        0x00, 0x03,                                /* Color Mode: RGB */
        0x00, 0x00, 0x00, 0x00,                    /* Color Data Length: 0 */
        /* Resources section (16 bytes total for one resource) */
        0x00, 0x00, 0x00, 0x10,                    /* Resources Length: 16 */
        /* Resource block */
        0x38, 0x42, 0x49, 0x4D,                    /* Signature "8BIM" */
        0x03, 0xED,                                /* ID: 1005 (resolution) */
        0x00, 0x00,                                /* Pascal name length: 0 (empty) */
        0x00, 0x00, 0x00, 0x04,                    /* Data length: 4 bytes */
        0xDE, 0xAD, 0xBE, 0xEF,                    /* Data */
        0x00, 0x00, 0x00, 0x00                     /* Layer/Mask info length: 0 */
    };

    size_t total_size = sizeof(psd_with_resource);
    uint8_t *combined = (uint8_t *)malloc(total_size);
    if (!combined) {
        fprintf(stderr, "ERROR: Out of memory\n");
        return 1;
    }
    memcpy(combined, psd_with_resource, sizeof(psd_with_resource));

    stream = psd_stream_create_buffer(NULL, combined, total_size);
    if (!stream) {
        fprintf(stderr, "ERROR: Failed to create stream with resources\n");
        free(combined);
        return 1;
    }

    doc = psd_parse(stream, NULL);
    if (!doc) {
        fprintf(stderr, "ERROR: Failed to parse PSD with resources\n");
        psd_stream_destroy(stream);
        free(combined);
        return 1;
    }

    psd_document_get_resource_count(doc, &resource_count);
    if (resource_count != 1) {
        fprintf(stderr, "ERROR: Expected 1 resource, got %zu\n", resource_count);
        psd_document_free(doc);
        psd_stream_destroy(stream);
        free(combined);
        return 1;
    }
    printf("  Resource block parsed OK\n");

    uint16_t res_id;
    const uint8_t *res_data;
    uint64_t res_length;
    psd_document_get_resource(doc, 0, &res_id, &res_data, &res_length);

    if (res_id != 0x03ED) {  /* 1005 */
        fprintf(stderr, "ERROR: Expected resource ID 1005, got %hu\n", res_id);
        psd_document_free(doc);
        psd_stream_destroy(stream);
        free(combined);
        return 1;
    }
    printf("  Resolution resource (ID 1005) found OK\n");

    if (res_length != 4 || memcmp(res_data, test_resource_data, 4) != 0) {
        fprintf(stderr, "ERROR: Resource data mismatch\n");
        psd_document_free(doc);
        psd_stream_destroy(stream);
        free(combined);
        return 1;
    }
    printf("  Resource data correct OK\n");

    /* Test find_resource */
    size_t found_index;
    psd_status_t status = psd_document_find_resource(doc, 0x03ED, &found_index);
    if (status != PSD_OK || found_index != 0) {
        fprintf(stderr, "ERROR: find_resource failed\n");
        psd_document_free(doc);
        psd_stream_destroy(stream);
        free(combined);
        return 1;
    }
    printf("  find_resource OK\n");

    /* Test finding non-existent resource */
    status = psd_document_find_resource(doc, 0xFFFF, &found_index);
    if (status == PSD_OK) {
        fprintf(stderr, "ERROR: Should not find non-existent resource\n");
        psd_document_free(doc);
        psd_stream_destroy(stream);
        free(combined);
        return 1;
    }
    printf("  Non-existent resource correctly not found OK\n");

    psd_document_free(doc);
    psd_stream_destroy(stream);
    free(combined);

    printf("  OK\n\n");
    return 0;
}

/**
 * @brief Test parsing a real PSD file from disk
 *
 * Tests basic parsing of an actual PSD file by:
 * 1. Reading the file from disk
 * 2. Creating a stream from the buffer
 * 3. Parsing the document
 * 4. Verifying basic properties
 * 5. Checking resources and layers
 */
static int test_sample_psd_file(const char *filename)
{
    printf("Testing real PSD file: %s\n", filename);
    
    /* Read file from disk */
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "  WARNING: Could not open file (may not exist)\n");
        return 0;  /* Not a failure, just skip */
    }
    
    /* Get file size */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > 100 * 1024 * 1024) {  /* 100MB limit */
        fprintf(stderr, "  ERROR: Invalid file size: %ld\n", file_size);
        fclose(f);
        return 1;
    }
    
    /* Read entire file */
    uint8_t *buffer = (uint8_t *)malloc(file_size);
    if (!buffer) {
        fprintf(stderr, "  ERROR: Could not allocate memory for file\n");
        fclose(f);
        return 1;
    }
    
    size_t bytes_read = fread(buffer, 1, file_size, f);
    fclose(f);
    
    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "  ERROR: Could not read entire file\n");
        free(buffer);
        return 1;
    }
    
    printf("  File size: %ld bytes\n", file_size);
    
    /* Create stream */
    psd_stream_t *stream = psd_stream_create_buffer(NULL, buffer, file_size);
    if (!stream) {
        fprintf(stderr, "  ERROR: Could not create stream\n");
        free(buffer);
        return 1;
    }
    
    /* Verify file signature at start */
    uint32_t signature;
    psd_status_t sig_status = psd_stream_read_be32(stream, &signature);
    if (sig_status != PSD_OK) {
        fprintf(stderr, "  ERROR: Could not read file signature\n");
        psd_stream_destroy(stream);
        free(buffer);
        return 1;
    }
    
    if (signature != 0x38425053) {  /* "8BPS" */
        fprintf(stderr, "  ERROR: Invalid file signature 0x%08x (expected 0x38425053)\n", signature);
        psd_stream_destroy(stream);
        free(buffer);
        return 1;
    }
    
    /* Seek back to start for actual parsing */
    psd_stream_seek(stream, 0);
    
    /* Read basic header info for diagnostics */
    uint32_t sig_check;
    uint16_t version_check, channels_check, depth_check, color_mode_check;
    uint32_t width_check, height_check;
    uint8_t reserved[6];
    
    psd_stream_read_be32(stream, &sig_check);
    psd_stream_read_be16(stream, &version_check);
    psd_stream_read_exact(stream, reserved, 6);
    psd_stream_read_be16(stream, &channels_check);
    psd_stream_read_be32(stream, &height_check);
    psd_stream_read_be32(stream, &width_check);
    psd_stream_read_be16(stream, &depth_check);
    psd_stream_read_be16(stream, &color_mode_check);
    
    psd_stream_seek(stream, 0);
    
    /* Parse document - capture status for debugging */
    psd_status_t parse_status = PSD_OK;
    psd_document_t *doc = psd_parse_ex(stream, NULL, &parse_status);
    if (!doc) {
        fprintf(stderr, "  FAILED: Could not parse PSD file\n");
        fprintf(stderr, "  Parse status: %d (%s)\n",
                (int)parse_status, psd_error_string(parse_status));
        fprintf(stderr, "  Diagnostic information:\n");
        fprintf(stderr, "    - File size: %ld bytes\n", file_size);
        fprintf(stderr, "    - File signature: 0x%08x (expected 0x38425053) %s\n", 
                sig_check, (sig_check == 0x38425053) ? "✓" : "✗");
        fprintf(stderr, "    - Version: %u (1=PSD, 2=PSB)\n", version_check);
        fprintf(stderr, "    - Dimensions: %ux%u\n", width_check, height_check);
        fprintf(stderr, "    - Channels: %u\n", channels_check);
        fprintf(stderr, "    - Depth: %u bits\n", depth_check);
        fprintf(stderr, "    - Color mode: %u\n", color_mode_check);
        psd_stream_destroy(stream);
        free(buffer);
        return 1;  /* Return 1 (failure) */
    }
    
    printf("  Parsed successfully\n");
    
    /* Check basic properties */
    uint32_t width, height;
    psd_status_t status = psd_document_get_dimensions(doc, &width, &height);
    if (status != PSD_OK) {
        fprintf(stderr, "  ERROR: Could not get dimensions\n");
        psd_document_free(doc);
        psd_stream_destroy(stream);
        free(buffer);
        return 1;
    }
    printf("  Dimensions: %ux%u\n", width, height);
    
    /* Check depth */
    uint16_t depth;
    status = psd_document_get_depth(doc, &depth);
    if (status != PSD_OK) {
        fprintf(stderr, "  ERROR: Could not get depth\n");
        psd_document_free(doc);
        psd_stream_destroy(stream);
        free(buffer);
        return 1;
    }
    printf("  Depth: %u bits\n", depth);
    
    /* Check color mode */
    psd_color_mode_t color_mode;
    status = psd_document_get_color_mode(doc, &color_mode);
    if (status != PSD_OK) {
        fprintf(stderr, "  ERROR: Could not get color mode\n");
        psd_document_free(doc);
        psd_stream_destroy(stream);
        free(buffer);
        return 1;
    }
    printf("  Color mode: %d (%s)\n", (int)color_mode, color_mode_name(color_mode));
    
    /* Check channels */
    uint16_t channels;
    status = psd_document_get_channels(doc, &channels);
    if (status != PSD_OK) {
        fprintf(stderr, "  ERROR: Could not get channels\n");
        psd_document_free(doc);
        psd_stream_destroy(stream);
        free(buffer);
        return 1;
    }
    printf("  Channels: %u\n", channels);
    
    /* Check if PSB */
    bool is_psb;
    status = psd_document_is_psb(doc, &is_psb);
    if (status != PSD_OK) {
        fprintf(stderr, "  ERROR: Could not check PSB flag\n");
        psd_document_free(doc);
        psd_stream_destroy(stream);
        free(buffer);
        return 1;
    }
    printf("  Format: %s\n", is_psb ? "PSB (Large Document)" : "PSD (Standard)");
    
    /* Check resources */
    size_t resource_count;
    status = psd_document_get_resource_count(doc, &resource_count);
    if (status != PSD_OK) {
        fprintf(stderr, "  ERROR: Could not get resource count\n");
        psd_document_free(doc);
        psd_stream_destroy(stream);
        free(buffer);
        return 1;
    }
    printf("  Resources: %zu\n", resource_count);
    
    /* Check layers */
    int32_t layer_count;
    status = psd_document_get_layer_count(doc, &layer_count);
    if (status != PSD_OK) {
        fprintf(stderr, "  ERROR: Could not get layer count\n");
        psd_document_free(doc);
        psd_stream_destroy(stream);
        free(buffer);
        return 1;
    }
    printf("  Layers: %d\n", layer_count);
    
    /* Check for transparency layer */
    bool has_transparency;
    status = psd_document_has_transparency_layer(doc, &has_transparency);
    if (status != PSD_OK) {
        fprintf(stderr, "  ERROR: Could not check transparency layer\n");
        psd_document_free(doc);
        psd_stream_destroy(stream);
        free(buffer);
        return 1;
    }
    printf("  Transparency layer: %s\n", has_transparency ? "yes" : "no");
    
    /* Check composite image */
    const uint8_t *composite_data = NULL;
    uint64_t composite_length = 0;
    uint32_t composite_compression = 0;
    status = psd_document_get_composite_image(doc, &composite_data, &composite_length, &composite_compression);
    if (status == PSD_OK && composite_data) {
        printf("  Composite image: %llu bytes, compression=%u\n", composite_length, composite_compression);
    } else if (status == PSD_ERR_UNSUPPORTED_COMPRESSION) {
        printf("  Composite image: Compressed with unsupported format (e.g., ZIP without zlib)\n");
    } else {
        printf("  Composite image: None or not available\n");
    }
    
    /* Try to access first few layers */
    if (layer_count > 0) {
        printf("  Checking ALL layers for 0x0 NORMAL layers:\n");
        int zero_size_count = 0;
        for (int32_t i = 0; i < layer_count; i++) {
            int32_t top, left, bottom, right;
            psd_document_get_layer_bounds(doc, i, &top, &left, &bottom, &right);
            int32_t width = right - left;
            int32_t height = bottom - top;
            
            psd_layer_features_t features;
            psd_document_get_layer_features(doc, i, &features);
            
            /* Check if it's a normal pixel layer (no special features) */
            bool is_normal_pixel = !features.is_group_start && !features.is_group_end &&
                                  !features.has_text && !features.has_smart_object && !features.has_adjustment &&
                                  !features.has_fill && !features.has_effects && !features.has_3d && !features.has_video;
            
            if (is_normal_pixel && (width == 0 || height == 0)) {
                zero_size_count++;
                printf("    Layer %d: NORMAL type but 0x0 size! bounds=(%d,%d)-(%d,%d)\n", i, left, top, right, bottom);
            }
        }
        if (zero_size_count == 0) {
            printf("    No NORMAL layers with 0x0 size found (good!)\n");
        }
        
        printf("  Checking first %d layer(s):\n", layer_count > 3 ? 3 : layer_count);
        for (int32_t i = 0; i < layer_count && i < 3; i++) {
            int32_t top, left, bottom, right;
            status = psd_document_get_layer_bounds(doc, i, &top, &left, &bottom, &right);
            if (status == PSD_OK) {
                int32_t width = right - left;
                int32_t height = bottom - top;
                printf("    Layer %d: bounds (%d,%d)-(%d,%d), size=%dx%d\n", i, left, top, right, bottom, width, height);
            } else {
                printf("    Layer %d: Failed to get bounds\n", i);
            }
            
            /* Check layer properties */
            uint8_t opacity, flags;
            status = psd_document_get_layer_properties(doc, i, &opacity, &flags);
            if (status == PSD_OK) {
                printf("    Layer %d: opacity=%u, flags=0x%02x\n", i, opacity, flags);
            }
            
            /* Check layer channels */
            size_t channel_count;
            status = psd_document_get_layer_channel_count(doc, i, &channel_count);
            if (status == PSD_OK) {
                printf("    Layer %d: %zu channel(s)\n", i, channel_count);
            }
            
            /* Check layer type from features */
            psd_layer_features_t features;
            status = psd_document_get_layer_features(doc, i, &features);
            if (status == PSD_OK) {
                const char *type_name = "Normal";
                if (features.is_group_start || features.is_group_end) {
                    type_name = "Group";
                } else if (features.has_text) {
                    type_name = "Text";
                } else if (features.has_smart_object) {
                    type_name = "SmartObject";
                } else if (features.has_adjustment) {
                    type_name = "Adjustment";
                } else if (features.has_fill) {
                    type_name = "Fill";
                } else if (features.has_effects) {
                    type_name = "Effects";
                } else if (features.has_3d) {
                    type_name = "3D";
                } else if (features.has_video) {
                    type_name = "Video";
                } else if (features.has_vector_mask) {
                    type_name = "Shape";
                }
                printf("    Layer %d: type=%s\n", i, type_name);
            }
        }
    }
    
    /* Cleanup */
    psd_document_free(doc);
    psd_stream_destroy(stream);
    free(buffer);
    
    printf("  OK\n\n");
    return 0;
}

static uint64_t calc_expected_channel_bytes(uint32_t width, uint32_t height, uint16_t depth_bits) {
    if (width == 0 || height == 0)
        return 0;

    if (depth_bits == 1) {
        /* 1-bit data is packed, row-aligned to whole bytes */
        uint64_t row_bytes = ((uint64_t)width + 7u) / 8u;
        return row_bytes * (uint64_t)height;
    }

    /* 8/16/32 */
    uint64_t bps = (uint64_t)(depth_bits / 8u);
    return (uint64_t)width * (uint64_t)height * bps;
}

static const char *channel_id_name(int16_t channel_id) {
    switch (channel_id) {
    case 0:
        return "R";
    case 1:
        return "G";
    case 2:
        return "B";
    case -1:
        return "A";
    default:
        return "OTHER";
    }
}

/**
 * @brief Test channel data parsing for a real PSD file
 *
 * Tests that channel data is properly loaded and can be decoded:
 * 1. Verifies channel data is loaded (not NULL)
 * 2. Verifies channel data can be accessed
 * 3. Verifies decoded data has correct size
 * 4. Tests different compression types
 */
static int test_channel_data_parsing(const char *filename) {
    printf("Testing channel data parsing: %s\n", filename);

    int failures = 0;

    /* Load + parse */
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "ERROR: could not open file: %s\n", filename);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz <= 0) {
        fprintf(stderr, "ERROR: file is empty: %s\n", filename);
        fclose(f);
        return 1;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)fsz);
    if (!buf) {
        fprintf(stderr, "ERROR: out of memory reading %s\n", filename);
        fclose(f);
        return 1;
    }

    if (fread(buf, 1, (size_t)fsz, f) != (size_t)fsz) {
        fprintf(stderr, "ERROR: failed to read file: %s\n", filename);
        free(buf);
        fclose(f);
        return 1;
    }
    fclose(f);

    psd_stream_t *stream = psd_stream_create_buffer(NULL, buf, (size_t)fsz);
    if (!stream) {
        fprintf(stderr, "ERROR: psd_stream_create_buffer failed\n");
        free(buf);
        return 1;
    }

    psd_document_t *doc = psd_parse(stream, NULL);
    if (!doc) {
        fprintf(stderr, "ERROR: psd_parse failed: %s\n", filename);
        psd_stream_destroy(stream);
        free(buf);
        return 1;
    }

    uint16_t depth_bits = 8;
    if (psd_document_get_depth(doc, &depth_bits) != PSD_OK) {
        fprintf(stderr, "ERROR: psd_document_get_depth failed\n");
        failures++;
    }

    int32_t layer_count = 0;
    if (psd_document_get_layer_count(doc, &layer_count) != PSD_OK) {
        fprintf(stderr, "ERROR: psd_document_get_layer_count failed\n");
        failures++;
        layer_count = 0;
    }

    int layers_seen = 0;
    int pixel_layers = 0;
    int pixel_layers_with_rgb = 0;

    for (int32_t i = 0; i < layer_count; i++) {
        psd_layer_type_t layer_type = PSD_LAYER_TYPE_EMPTY;
        if (psd_document_get_layer_type(doc, i, &layer_type) != PSD_OK) {
            fprintf(stderr, "ERROR: layer %d: failed to get layer type\n", i);
            failures++;
            continue;
        }

        layers_seen++;

        if (layer_type != PSD_LAYER_TYPE_PIXEL) {
            continue; /* only validating pixel layers here */
        }
        pixel_layers++;

        int32_t top, left, bottom, right;
        if (psd_document_get_layer_bounds(doc, i, &top, &left, &bottom, &right) != PSD_OK) {
            fprintf(stderr, "ERROR: layer %d: failed to get bounds\n", i);
            failures++;
            continue;
        }

        uint32_t w = (right > left) ? (uint32_t)(right - left) : 0;
        uint32_t h = (bottom > top) ? (uint32_t)(bottom - top) : 0;
        if (w == 0 || h == 0) {
            /* Valid PSD can have empty pixel layers; don't fail */
            continue;
        }

        size_t channel_count = 0;
        if (psd_document_get_layer_channel_count(doc, i, &channel_count) != PSD_OK) {
            fprintf(stderr, "ERROR: layer %d: failed to get channel count\n", i);
            failures++;
            continue;
        }
        if (channel_count == 0) {
            /* Empty/deleted raster layer is possible */
            continue;
        }

        const uint64_t expected = calc_expected_channel_bytes(w, h, depth_bits);
        if (expected == 0) {
            fprintf(stderr, "ERROR: layer %d: expected size computed as 0 (w=%u h=%u depth=%u)\n",
                    i, (unsigned)w, (unsigned)h, (unsigned)depth_bits);
            failures++;
            continue;
        }

        bool have_r = false, have_g = false, have_b = false, have_a = false;
        bool saw_any_color_plane = false;

        for (size_t ch = 0; ch < channel_count; ch++) {
            int16_t channel_id = 0;
            const uint8_t *data = NULL;
            uint64_t len = 0;
            uint32_t compression = 0;

            psd_status_t st = psd_document_get_layer_channel_data(
                doc, i, ch, &channel_id, &data, &len, &compression);
            if (st != PSD_OK) {
                fprintf(stderr, "ERROR: layer %d: channel[%zu] get failed (status=%d)\n", i, ch, (int)st);
                failures++;
                continue;
            }
            
            if (!data || len == 0) {
                /* Some channels can legitimately be absent; only fail if it's an RGB plane */
                if (channel_id == 0 || channel_id == 1 || channel_id == 2) {
                    fprintf(stderr, "ERROR: layer %d: %s channel missing (id=%d)\n",
                            i, channel_id_name(channel_id), channel_id);
                    failures++;
                }
                continue;
            }

            const bool is_color_plane = (channel_id == 0 || channel_id == 1 || channel_id == 2 || channel_id == -1);
            if (!is_color_plane) {
                /* Masks/spot/etc: retrieval success is enough; do not size-validate */
                continue;
            }

            saw_any_color_plane = true;

            /* Since the public API does lazy decoding, 'len' should describe decoded bytes.
             * RAW is allowed to include padding in some real-world writers; accept >= expected. */
            if (compression == 0) {
                if (len < expected) {
                    fprintf(stderr,
                            "ERROR: layer %d: %s RAW truncated: got=%llu expected_at_least=%llu\n",
                            i, channel_id_name(channel_id),
                            (unsigned long long)len,
                            (unsigned long long)expected);
                    failures++;
                    continue;
                }
                if (len != expected) {
                    fprintf(stderr,
                            "NOTE: layer %d: %s RAW padding: got=%llu expected=%llu (ignored=%llu)\n",
                            i, channel_id_name(channel_id),
                            (unsigned long long)len,
                            (unsigned long long)expected,
                            (unsigned long long)(len - expected));
                }
            } else if (compression == 1 || compression == 2 || compression == 3) {
                if (len != expected) {
                    fprintf(stderr,
                            "ERROR: layer %d: %s decoded size mismatch: got=%llu expected=%llu (compression=%u)\n",
                            i, channel_id_name(channel_id),
                            (unsigned long long)len,
                            (unsigned long long)expected,
                            (unsigned)compression);
                    failures++;
                    continue;
                }
            } else {
                fprintf(stderr,
                        "ERROR: layer %d: %s unknown compression=%u\n",
                        i, channel_id_name(channel_id), (unsigned)compression);
                failures++;
                continue;
            }

            if (channel_id == 0)
                have_r = true;
            if (channel_id == 1)
                have_g = true;
            if (channel_id == 2)
                have_b = true;
            if (channel_id == -1)
                have_a = true;

            /* Optional: once we have RGB, we're happy */
            if (have_r && have_g && have_b) {
                break;
            }
        }

        if (saw_any_color_plane) {
            if (have_r && have_g && have_b) {
                pixel_layers_with_rgb++;
            } else {
                fprintf(stderr,
                        "ERROR: layer %d: missing required RGB planes (R=%d G=%d B=%d A=%d)\n",
                        i, (int)have_r, (int)have_g, (int)have_b, (int)have_a);
                failures++;
            }
        } else {
            /* Pixel layer with channels but none were RGB(A) -> unusual; warn */
            fprintf(stderr, "WARNING: layer %d: pixel layer has no RGB(A) planes\n", i);
        }
    }

    printf("Summary: layers=%d pixel_layers=%d pixel_layers_with_rgb=%d failures=%d\n",
           layers_seen, pixel_layers, pixel_layers_with_rgb, failures);

    psd_document_free(doc);
    psd_stream_destroy(stream);
    free(buf);

    return (failures == 0) ? 0 : 1;
}

int run_basic_tests(void)
{
    printf("========================================\n");
    printf("OpenPSD Library - Basic Tests\n");
    printf("========================================\n\n");

    int failures = 0;

    failures += test_version();
    failures += test_error_strings();
    failures += test_stream();
    failures += test_allocator();
    failures += test_endian_readers();
    failures += test_length_reader();
    failures += test_psd_header_parsing();
    failures += test_invalid_headers();
    failures += test_color_mode_data_parsing();
    failures += test_image_resources_parsing();
    
    /* Test with real PSD files */
    printf("Testing with real PSD files...\n\n");
    char p1[512], p2[512], p3[512], p4[512];
    join_path(p1, sizeof(p1), OPENPSD_TEST_SAMPLES_DIR, "sample-2.psd");
    join_path(p2, sizeof(p2), OPENPSD_TEST_SAMPLES_DIR, "sample-5.psd");
    join_path(p3, sizeof(p3), OPENPSD_TEST_SAMPLES_DIR, "rockstar.psd");
    join_path(p4, sizeof(p4), OPENPSD_TEST_SAMPLES_DIR, "Fei.psb");
    failures += test_sample_psd_file(p1);
    failures += test_sample_psd_file(p2);
    failures += test_sample_psd_file(p3);
    failures += test_sample_psd_file(p4);
    
    /* Test channel data parsing */
    printf("Testing channel data parsing...\n\n");
    failures += test_channel_data_parsing(p1);
    failures += test_channel_data_parsing(p2);
    failures += test_channel_data_parsing(p3);
    failures += test_channel_data_parsing(p4);

    printf("========================================\n");
    if (failures == 0) {
        printf("All tests PASSED\n");
        return 0;
    } else {
        printf("Tests FAILED (%d failures)\n", failures);
        return 1;
    }
}
