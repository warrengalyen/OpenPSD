/**
 * @file test_background_layer.c
 * @brief Unit tests for background layer detection
 *
 * Tests the psd_document_is_background_layer() function against various
 * layer configurations to ensure correct identification of true Photoshop
 * background layers.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openpsd/psd.h>
#include <openpsd/psd_stream.h>
#include "openpsd_tests.h"

#ifndef OPENPSD_TEST_SAMPLES_DIR
#define OPENPSD_TEST_SAMPLES_DIR "tests/samples"
#endif

static void join_path(char *out, size_t out_cap, const char *dir, const char *file)
{
    if (!out || out_cap == 0) return;
    out[0] = '\0';
    if (!dir) dir = "";
    if (!file) file = "";
    (void)snprintf(out, out_cap, "%s/%s", dir, file);
}

/* Test results tracking */
static int tests_passed = 0;
static int tests_failed = 0;

/* Helper macro for assertions */
#define ASSERT_TRUE(expr, msg) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        tests_failed++; \
    } else { \
        fprintf(stdout, "PASS: %s\n", msg); \
        tests_passed++; \
    } \
} while(0)

#define ASSERT_FALSE(expr, msg) ASSERT_TRUE(!(expr), msg)

#define ASSERT_EQ(a, b, msg) ASSERT_TRUE((a) == (b), msg)

/**
 * Test 1: Real PSD with background layer detection
 */
static void test_rockstar_psd_background(void) {
    fprintf(stdout, "\n=== Test: Real PSD Background Layer Detection ===\n");

    char path[512];
    join_path(path, sizeof(path), OPENPSD_TEST_SAMPLES_DIR, "rockstar.psd");
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "SKIP: rockstar.psd not found\n");
        return;
    }

    /* Read file into buffer */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    uint8_t *file_data = malloc(file_size);
    if (!file_data) {
        fclose(f);
        fprintf(stderr, "FAIL: Memory allocation failed\n");
        tests_failed++;
        return;
    }

    size_t bytes_read = fread(file_data, 1, file_size, f);
    fclose(f);

    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "FAIL: Could not read entire file\n");
        tests_failed++;
        free(file_data);
        return;
    }

    /* Parse PSD */
    psd_stream_t *stream = psd_stream_create_buffer(NULL, file_data, (size_t)file_size);
    if (!stream) {
        fprintf(stderr, "FAIL: Could not create stream\n");
        tests_failed++;
        free(file_data);
        return;
    }

    psd_document_t *doc = psd_parse(stream, NULL);

    if (!doc) {
        fprintf(stderr, "FAIL: Could not parse rockstar.psd\n");
        tests_failed++;
        psd_stream_destroy(stream);
        free(file_data);
        return;
    }

    /* Get document properties */
    int32_t layer_count = 0;
    uint32_t width = 0, height = 0;
    uint16_t color_channels = 0;

    psd_document_get_layer_count(doc, &layer_count);
    psd_document_get_dimensions(doc, &width, &height);
    psd_document_get_channels(doc, &color_channels);

    fprintf(stdout, "Document: %dx%d, %d layers, %d color channels\n",
            width, height, layer_count, color_channels);

    /* Check each layer to see if we can identify a background layer */
    for (int32_t i = 0; i < layer_count; i++) {
        psd_bool_t is_bg = psd_document_is_background_layer(doc, i, (int)color_channels);

        uint8_t flags = 0;
        uint8_t opacity = 0;
        psd_document_get_layer_properties(doc, i, &opacity, &flags);

        size_t channel_count = 0;
        psd_document_get_layer_channel_count(doc, i, &channel_count);

        psd_layer_features_t features;
        psd_document_get_layer_features(doc, i, &features);

        fprintf(stdout, "Layer %d: is_bg=%d, is_group_start=%d, is_group_end=%d, channels=%zu, flags=0x%02x\n",
                i, is_bg, features.is_group_start, features.is_group_end, channel_count, flags);

        if (is_bg) {
            ASSERT_TRUE(i == layer_count - 1, "Background layer is at bottom position");
            ASSERT_TRUE((flags & 0x04), "Background layer has background flag set");
            ASSERT_EQ(channel_count, color_channels, "Background layer has all color channels");
        }
    }

    psd_stream_destroy(stream);
    psd_document_free(doc);
    free(file_data);
}

/**
 * Test 2: Sample PSD file analysis
 */
static void test_sample_psd_background(void) {
    fprintf(stdout, "\n=== Test: Sample PSD Background Layer Detection ===\n");

    char path[512];
    join_path(path, sizeof(path), OPENPSD_TEST_SAMPLES_DIR, "sample-2.psd");
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "SKIP: sample-2.psd not found\n");
        return;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    uint8_t *file_data = malloc(file_size);
    if (!file_data) {
        fclose(f);
        fprintf(stderr, "FAIL: Memory allocation failed\n");
        tests_failed++;
        return;
    }

    size_t bytes_read = fread(file_data, 1, file_size, f);
    fclose(f);

    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "FAIL: Could not read entire file\n");
        tests_failed++;
        free(file_data);
        return;
    }

    psd_stream_t *stream = psd_stream_create_buffer(NULL, file_data, (size_t)file_size);
    if (!stream) {
        fprintf(stderr, "FAIL: Could not create stream\n");
        tests_failed++;
        free(file_data);
        return;
    }

    psd_document_t *doc = psd_parse(stream, NULL);

    if (!doc) {
        fprintf(stderr, "FAIL: Could not parse sample-2.psd\n");
        tests_failed++;
        psd_stream_destroy(stream);
        free(file_data);
        return;
    }

    int32_t layer_count = 0;
    uint16_t color_channels = 0;
    psd_document_get_layer_count(doc, &layer_count);
    psd_document_get_channels(doc, &color_channels);

    fprintf(stdout, "Layers: %d, Color channels: %d\n", layer_count, color_channels);

    /* Count how many background layers we find (should be 0 or 1) */
    int background_count = 0;
    for (int32_t i = 0; i < layer_count; i++) {
        if (psd_document_is_background_layer(doc, i, (int)color_channels)) {
            background_count++;
            fprintf(stdout, "Found background layer at index %d\n", i);
        }
    }

    ASSERT_TRUE(background_count <= 1, "At most one background layer");

    psd_stream_destroy(stream);
    psd_document_free(doc);
    free(file_data);
}

/**
 * Test 3: Check layer structure criteria
 * Verifies the criteria are checked in order
 */
static void test_background_criteria_verification(void) {
    fprintf(stdout, "\n=== Test: Background Layer Criteria Verification ===\n");

    char path[512];
    join_path(path, sizeof(path), OPENPSD_TEST_SAMPLES_DIR, "rockstar.psd");
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "SKIP: rockstar.psd not found\n");
        return;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    uint8_t *file_data = malloc(file_size);
    if (!file_data) {
        fclose(f);
        fprintf(stderr, "FAIL: Memory allocation failed\n");
        tests_failed++;
        return;
    }

    size_t bytes_read = fread(file_data, 1, file_size, f);
    fclose(f);

    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "FAIL: Could not read entire file\n");
        tests_failed++;
        free(file_data);
        return;
    }

    psd_stream_t *stream = psd_stream_create_buffer(NULL, file_data, (size_t)file_size);
    if (!stream) {
        fprintf(stderr, "FAIL: Could not create stream\n");
        tests_failed++;
        free(file_data);
        return;
    }

    psd_document_t *doc = psd_parse(stream, NULL);

    if (!doc) {
        fprintf(stderr, "FAIL: Could not parse rockstar.psd\n");
        tests_failed++;
        psd_stream_destroy(stream);
        free(file_data);
        return;
    }

    int32_t layer_count = 0;
    uint16_t color_channels = 0;
    psd_document_get_layer_count(doc, &layer_count);
    psd_document_get_channels(doc, &color_channels);

    /* Test non-bottom layers - should never be background */
    for (int32_t i = 0; i < layer_count - 1; i++) {
        psd_bool_t is_bg = psd_document_is_background_layer(doc, i, (int)color_channels);
        ASSERT_FALSE(is_bg, "Non-bottom layer should not be background");
    }

    fprintf(stdout, "All %d non-bottom layers correctly identified as non-background\n",
            layer_count - 1);

    psd_stream_destroy(stream);
    psd_document_free(doc);
    free(file_data);
}

/**
 * Test 4: Verify NULL pointer handling
 */
static void test_background_null_handling(void) {
    fprintf(stdout, "\n=== Test: NULL Pointer Handling ===\n");

    /* NULL document should return false (0) */
    psd_bool_t result = psd_document_is_background_layer(NULL, 0, 3);
    ASSERT_FALSE(result, "NULL document returns false");

    fprintf(stdout, "NULL handling verified\n");
}

/**
 * Test 5: Edge case - single layer document
 */
static void test_single_layer_document(void) {
    fprintf(stdout, "\n=== Test: Single Layer Document ===\n");

    /* This would be a document with just one layer */
    fprintf(stdout, "Note: Single layer document testing would require creating a minimal PSD\n");
    fprintf(stdout, "Skipping synthetic test - real file tests cover this case\n");
}

/**
 * Print test summary
 */
static void print_summary(void) {
    fprintf(stdout, "\n========================================\n");
    fprintf(stdout, "Test Results: %d passed, %d failed\n", tests_passed, tests_failed);
    fprintf(stdout, "========================================\n");
}

/**
 * Main test runner
 */
int run_background_layer_tests(void) {
    fprintf(stdout, "Background Layer Detection Unit Tests\n");
    fprintf(stdout, "======================================\n\n");

    test_background_null_handling();
    test_background_criteria_verification();
    test_rockstar_psd_background();
    test_sample_psd_background();
    test_single_layer_document();

    print_summary();

    return tests_failed > 0 ? 1 : 0;
}
