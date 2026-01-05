/**
 * @file test_text_layers.c
 * @brief Text layer phase-1 API tests
 *
 * Verifies that text layers can be accessed via the minimal public API:
 * - Text string
 * - One default style (font name, size, color, tracking, leading, justification)
 * - Transform matrix + bounds
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

static FILE *open_candidate_paths(const char *path, char *resolved, size_t resolved_sz)
{
    if (!path || !resolved || resolved_sz == 0) return NULL;

    /* 1) As provided */
    snprintf(resolved, resolved_sz, "%s", path);
    FILE *f = fopen(resolved, "rb");
    if (f) return f;

    /* 2) One level up */
    snprintf(resolved, resolved_sz, "../%s", path);
    f = fopen(resolved, "rb");
    if (f) return f;

    /* 3) Two levels up */
    snprintf(resolved, resolved_sz, "../../%s", path);
    return fopen(resolved, "rb");
}

static psd_document_t *parse_file(const char *path)
{
    char resolved[1024] = {0};
    FILE *f = open_candidate_paths(path, resolved, sizeof(resolved));
    if (!f) {
        fprintf(stderr, "    ERROR: cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        fprintf(stderr, "    ERROR: empty file '%s'\n", path);
        return NULL;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "    ERROR: OOM reading '%s'\n", path);
        return NULL;
    }

    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) {
        free(buf);
        fprintf(stderr, "    ERROR: short read '%s'\n", path);
        return NULL;
    }

    psd_stream_t *stream = psd_stream_create_buffer(NULL, buf, (size_t)sz);
    if (!stream) {
        free(buf);
        fprintf(stderr, "    ERROR: psd_stream_create_buffer failed\n");
        return NULL;
    }

    psd_document_t *doc = psd_parse(stream, NULL);
    psd_stream_destroy(stream);
    free(buf);
    return doc;
}

static const char *just_name(psd_text_justification_t j)
{
    switch (j) {
        case PSD_TEXT_JUSTIFY_LEFT: return "left";
        case PSD_TEXT_JUSTIFY_RIGHT: return "right";
        case PSD_TEXT_JUSTIFY_CENTER: return "center";
        case PSD_TEXT_JUSTIFY_FULL: return "full";
        default: return "left";
    }
}

static int test_real_psd_files(void)
{
    const char *files[] = {
        "tests/samples/text-layers/text-layer.psd",
        "tests/samples/tianye.psd",
    };

    int failures = 0;
    printf("\nTesting real PSD file parsing (phase 1 text API)...\n");

    for (size_t fi = 0; fi < sizeof(files) / sizeof(files[0]); fi++) {
        printf("  Testing: %s\n", files[fi]);
        psd_document_t *doc = parse_file(files[fi]);
        if (!doc) {
            failures++;
            continue;
        }

        int32_t layer_count = 0;
        psd_document_get_layer_count(doc, &layer_count);
        printf("    Layers: %d\n", (int)layer_count);

        int found_text = 0;
        for (int32_t li = 0; li < layer_count; li++) {
            psd_layer_type_t t;
            if (psd_document_get_layer_type(doc, li, &t) != PSD_OK) continue;
            if (t != PSD_LAYER_TYPE_TEXT) continue;
            found_text++;

            char text[1024] = {0};
            psd_status_t st_text = psd_text_layer_get_text(doc, (uint32_t)li, text, sizeof(text));
            if (st_text != PSD_OK || text[0] == '\0') {
                fprintf(stderr, "    ERROR: layer %d text failed: %s (%d)\n",
                        (int)li, psd_error_string(st_text), (int)st_text);
                failures++;
                break;
            }

            psd_text_style_t style;
            psd_status_t st_style = psd_text_layer_get_default_style(doc, (uint32_t)li, &style);
            if (st_style != PSD_OK || style.font_name[0] == '\0' || style.size <= 0.0) {
                fprintf(stderr, "    ERROR: layer %d style failed: %s (%d)\n",
                        (int)li, psd_error_string(st_style), (int)st_style);
                failures++;
                break;
            }

            psd_text_matrix_t m;
            psd_text_bounds_t b;
            psd_status_t st_mb = psd_text_layer_get_matrix_bounds(doc, (uint32_t)li, &m, &b);
            if (st_mb != PSD_OK) {
                fprintf(stderr, "    ERROR: layer %d matrix/bounds failed: %s (%d)\n",
                        (int)li, psd_error_string(st_mb), (int)st_mb);
                failures++;
                break;
            }

            printf("    Text layer index=%d\n", (int)li);
            printf("      Text: %.80s\n", text);
            printf("      Font: %s\n", style.font_name);
            printf("      Size: %.2f\n", style.size);
            printf("      Color: rgba(%u,%u,%u,%u)\n",
                   (unsigned)style.color_rgba[0], (unsigned)style.color_rgba[1],
                   (unsigned)style.color_rgba[2], (unsigned)style.color_rgba[3]);
            printf("      Tracking: %.2f\n", style.tracking);
            printf("      Leading: %.2f\n", style.leading);
            printf("      Justification: %s\n", just_name(style.justification));
            printf("      Transform: (%.2f, %.2f) (%.2f, %.2f) (%.2f, %.2f)\n",
                   m.xx, m.xy, m.yx, m.yy, m.tx, m.ty);
            printf("      Bounds: (%.0f, %.0f) - (%.0f, %.0f)\n",
                   b.left, b.top, b.right, b.bottom);

            /* Validating one is sufficient for this test */
            break;
        }

        if (found_text == 0) {
            fprintf(stderr, "    ERROR: Expected at least one text layer in fixture\n");
            failures++;
        }

        psd_document_free(doc);
        printf("    OK\n");
    }

    return failures == 0 ? 0 : 1;
}

static int test_null_safety(void)
{
    printf("\nTesting NULL pointer safety...\n");

    char buf[16] = {0};
    psd_text_style_t style;
    psd_text_matrix_t m;
    psd_text_bounds_t b;

    if (psd_text_layer_get_text(NULL, 0, buf, sizeof(buf)) == PSD_OK) {
        fprintf(stderr, "ERROR: Expected error for NULL doc (get_text)\n");
        return 1;
    }
    if (psd_text_layer_get_default_style(NULL, 0, &style) == PSD_OK) {
        fprintf(stderr, "ERROR: Expected error for NULL doc (get_default_style)\n");
        return 1;
    }
    if (psd_text_layer_get_matrix_bounds(NULL, 0, &m, &b) == PSD_OK) {
        fprintf(stderr, "ERROR: Expected error for NULL doc (get_matrix_bounds)\n");
        return 1;
    }

    printf("  OK\n");
    return 0;
}

int run_text_layer_tests(void)
{
    printf("========================================\n");
    printf("OpenPSD Library - Text Layer Tests (Phase 1 API)\n");
    printf("========================================\n");

    int failed = 0;
    failed += test_real_psd_files();
    failed += test_null_safety();

    printf("========================================\n");
    if (failed == 0) {
        printf("All tests PASSED\n");
    } else {
        printf("Tests FAILED (%d)\n", failed);
    }
    printf("========================================\n");
    return failed == 0 ? 0 : 1;
}

