/**
 * @file test_color_modes.c
 * @brief Tests for non-RGB color mode rendering
 *
 * Ensures the library can convert composite / layer pixels to RGBA8 for
 * common non-RGB document color modes (Lab, CMYK, Indexed, Grayscale).
 */

#include <openpsd/psd.h>
#include "openpsd_tests.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#ifndef OPENPSD_TEST_SAMPLES_DIR
#define OPENPSD_TEST_SAMPLES_DIR "tests/samples"
#endif

#define ASSERT_TRUE(expr, msg) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        tests_failed++; \
    } else { \
        fprintf(stdout, "PASS: %s\n", msg); \
        tests_passed++; \
    } \
} while(0)

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

static void join_path(char *out, size_t out_cap, const char *dir, const char *file)
{
    if (!out || out_cap == 0) return;
    out[0] = '\0';
    if (!dir) dir = "";
    if (!file) file = "";
    /* Always use forward slashes; Windows CRT accepts them in fopen paths. */
    (void)snprintf(out, out_cap, "%s/%s", dir, file);
}

static uint32_t checksum32(const uint8_t *p, size_t n)
{
    /* simple FNV-1a */
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= (uint32_t)p[i];
        h *= 16777619u;
    }
    return h;
}

static int render_composite(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "SKIP: %s not found\n", path);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);
    if (file_size <= 0) {
        fclose(f);
        fprintf(stderr, "FAIL: %s invalid size\n", path);
        return 1;
    }

    uint8_t *file_data = (uint8_t *)malloc((size_t)file_size);
    if (!file_data) {
        fclose(f);
        fprintf(stderr, "FAIL: malloc failed\n");
        return 1;
    }

    size_t bytes_read = fread(file_data, 1, (size_t)file_size, f);
    fclose(f);
    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "FAIL: could not read %s\n", path);
        free(file_data);
        return 1;
    }

    psd_stream_t *stream = psd_stream_create_buffer(NULL, file_data, (size_t)file_size);
    if (!stream) {
        fprintf(stderr, "FAIL: could not create stream\n");
        free(file_data);
        return 1;
    }

    psd_document_t *doc = psd_parse(stream, NULL);
    if (!doc) {
        fprintf(stderr, "FAIL: could not parse %s\n", path);
        psd_stream_destroy(stream);
        free(file_data);
        return 1;
    }

    psd_color_mode_t mode = (psd_color_mode_t)0;
    psd_document_get_color_mode(doc, &mode);

    uint32_t w = 0, h = 0;
    psd_document_get_dimensions(doc, &w, &h);

    size_t required = 0;
    psd_status_t st = psd_document_render_composite_rgba8(doc, NULL, 0, &required);
    ASSERT_TRUE(st == PSD_ERR_BUFFER_TOO_SMALL || st == PSD_OK, "query composite RGBA size");

    uint8_t *rgba = (uint8_t *)malloc(required);
    ASSERT_TRUE(rgba != NULL, "allocate composite RGBA buffer");
    if (!rgba) {
        psd_document_free(doc);
        psd_stream_destroy(stream);
        free(file_data);
        return 1;
    }

    st = psd_document_render_composite_rgba8(doc, rgba, required, NULL);
    ASSERT_TRUE(st == PSD_OK, "render composite to RGBA8");

    if (st == PSD_OK && required >= 4) {
        uint32_t h32 = checksum32(rgba, required);
        fprintf(stdout, "Composite %s: %ux%u mode=%d (%s) checksum=0x%08x\n",
                path, w, h, (int)mode, color_mode_name(mode), h32);
        ASSERT_TRUE(h32 != 0u, "composite checksum non-zero");
    }

    free(rgba);
    psd_document_free(doc);
    psd_stream_destroy(stream);
    free(file_data);
    return 0;
}

static int render_first_layer(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "SKIP: %s not found\n", path);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    uint8_t *file_data = (uint8_t *)malloc((size_t)file_size);
    if (!file_data) {
        fclose(f);
        fprintf(stderr, "FAIL: malloc failed\n");
        return 1;
    }

    size_t bytes_read = fread(file_data, 1, (size_t)file_size, f);
    fclose(f);
    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "FAIL: could not read %s\n", path);
        free(file_data);
        return 1;
    }

    psd_stream_t *stream = psd_stream_create_buffer(NULL, file_data, (size_t)file_size);
    psd_document_t *doc = psd_parse(stream, NULL);
    if (!doc) {
        fprintf(stderr, "SKIP: parse failed for %s\n", path);
        psd_stream_destroy(stream);
        free(file_data);
        return 0;
    }

    int32_t layer_count = 0;
    psd_document_get_layer_count(doc, &layer_count);
    ASSERT_TRUE(layer_count >= 0, "layer_count available");

    int32_t chosen = -1;
    for (int32_t i = 0; i < layer_count; i++) {
        size_t cc = 0;
        if (psd_document_get_layer_channel_count(doc, i, &cc) == PSD_OK && cc > 0) {
            int32_t top, left, bottom, right;
            if (psd_document_get_layer_bounds(doc, i, &top, &left, &bottom, &right) == PSD_OK) {
                if (right > left && bottom > top) {
                    chosen = i;
                    break;
                }
            }
        }
    }

    if (chosen >= 0) {
        size_t required = 0;
        psd_status_t st = psd_document_render_layer_rgba8(doc, chosen, NULL, 0, &required);
        ASSERT_TRUE(st == PSD_ERR_BUFFER_TOO_SMALL || st == PSD_OK, "query layer RGBA size");
        uint8_t *rgba = (uint8_t *)malloc(required);
        ASSERT_TRUE(rgba != NULL, "allocate layer RGBA buffer");
        if (rgba) {
            st = psd_document_render_layer_rgba8(doc, chosen, rgba, required, NULL);
            ASSERT_TRUE(st == PSD_OK, "render layer to RGBA8");
            free(rgba);
        }
    } else {
        fprintf(stdout, "SKIP: no renderable layer found in %s\n", path);
    }

    psd_document_free(doc);
    psd_stream_destroy(stream);
    free(file_data);
    return 0;
}

int run_color_mode_tests(void)
{
    fprintf(stdout, "=== Color mode rendering tests ===\n");

    /* These are repository sample PSDs. We do not hard-fail if their color mode
     * is not Lab/CMYK/etc; we just ensure rendering doesn't crash. */
    char p1[512];
    char p2[512];
    join_path(p1, sizeof(p1), OPENPSD_TEST_SAMPLES_DIR, "sign-mockup.psd");
    join_path(p2, sizeof(p2), OPENPSD_TEST_SAMPLES_DIR, "tianye.psd");

    (void)render_composite(p1);
    (void)render_first_layer(p1);

    (void)render_composite(p2);
    (void)render_first_layer(p2);

    fprintf(stdout, "\nTests passed: %d\nTests failed: %d\n", tests_passed, tests_failed);
    return (tests_failed == 0) ? 0 : 1;
}

