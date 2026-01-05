/**
 * @file psd_render.c
 * @brief Color mode aware rendering helpers (RGBA8)
 *
 * Converts decoded planar PSD pixel data (composite or layer channels) into
 * interleaved RGBA8 for display.
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

#include <math.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>

/* ----------------------------
 * Small helpers
 * ---------------------------- */

static inline uint32_t bytes_per_sample(uint16_t depth_bits)
{
    if (depth_bits == 8) return 1;
    if (depth_bits == 16) return 2;
    if (depth_bits == 32) return 4;
    if (depth_bits == 1) return 0; /* packed bits */
    return 1;
}

static inline uint16_t read_be_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static inline uint8_t sample_to_u8(const uint8_t *p, uint16_t depth_bits)
{
    if (depth_bits == 8) return p[0];
    if (depth_bits == 16) return p[0]; /* MSB of big-endian 16-bit */
    if (depth_bits == 32) return p[0]; /* TODO: float conversion */
    return p[0];
}

static inline float clamp01f(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static inline uint8_t float_to_u8(float v)
{
    v = clamp01f(v);
    int iv = (int)lroundf(v * 255.0f);
    if (iv < 0) iv = 0;
    if (iv > 255) iv = 255;
    return (uint8_t)iv;
}

static inline float srgb_compand(float v)
{
    v = clamp01f(v);
    if (v <= 0.0031308f) return 12.92f * v;
    return 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
}

static void lab_d50_to_srgb_u8(float L, float a, float b, uint8_t *out_rgb)
{
    /* Convert CIE Lab (D50) -> XYZ (D50) */
    const float fy = (L + 16.0f) / 116.0f;
    const float fx = fy + (a / 500.0f);
    const float fz = fy - (b / 200.0f);

    const float eps = 216.0f / 24389.0f;  /* 0.008856... */
    const float kappa = 24389.0f / 27.0f; /* 903.3... */

    float fx3 = fx * fx * fx;
    float fy3 = fy * fy * fy;
    float fz3 = fz * fz * fz;

    float xr = (fx3 > eps) ? fx3 : ((116.0f * fx - 16.0f) / kappa);
    float yr = (L > (kappa * eps)) ? fy3 : (L / kappa);
    float zr = (fz3 > eps) ? fz3 : ((116.0f * fz - 16.0f) / kappa);

    /* D50 reference white (ICC) */
    float X = xr * 0.96422f;
    float Y = yr * 1.0f;
    float Z = zr * 0.82521f;

    /* Bradford adaptation D50 -> D65 */
    /* B and B^-1 matrices */
    const float Bm[3][3] = {
        { 0.8951f,  0.2664f, -0.1614f },
        { -0.7502f, 1.7135f,  0.0367f },
        { 0.0389f, -0.0685f,  1.0296f }
    };
    const float Bi[3][3] = {
        { 0.9869929f, -0.1470543f, 0.1599627f },
        { 0.4323053f,  0.5183603f, 0.0492912f },
        { -0.0085287f, 0.0400428f, 0.9684867f }
    };

    /* Whitepoints in XYZ */
    const float D50[3] = { 0.96422f, 1.0f, 0.82521f };
    const float D65[3] = { 0.95047f, 1.0f, 1.08883f };

    /* LMS for source/target whites */
    float LMS50[3] = {
        Bm[0][0]*D50[0] + Bm[0][1]*D50[1] + Bm[0][2]*D50[2],
        Bm[1][0]*D50[0] + Bm[1][1]*D50[1] + Bm[1][2]*D50[2],
        Bm[2][0]*D50[0] + Bm[2][1]*D50[1] + Bm[2][2]*D50[2]
    };
    float LMS65[3] = {
        Bm[0][0]*D65[0] + Bm[0][1]*D65[1] + Bm[0][2]*D65[2],
        Bm[1][0]*D65[0] + Bm[1][1]*D65[1] + Bm[1][2]*D65[2],
        Bm[2][0]*D65[0] + Bm[2][1]*D65[1] + Bm[2][2]*D65[2]
    };

    float lms[3] = {
        Bm[0][0]*X + Bm[0][1]*Y + Bm[0][2]*Z,
        Bm[1][0]*X + Bm[1][1]*Y + Bm[1][2]*Z,
        Bm[2][0]*X + Bm[2][1]*Y + Bm[2][2]*Z
    };

    /* scale LMS */
    if (LMS50[0] != 0.0f) lms[0] *= (LMS65[0] / LMS50[0]);
    if (LMS50[1] != 0.0f) lms[1] *= (LMS65[1] / LMS50[1]);
    if (LMS50[2] != 0.0f) lms[2] *= (LMS65[2] / LMS50[2]);

    /* back to XYZ (D65) */
    float Xd = Bi[0][0]*lms[0] + Bi[0][1]*lms[1] + Bi[0][2]*lms[2];
    float Yd = Bi[1][0]*lms[0] + Bi[1][1]*lms[1] + Bi[1][2]*lms[2];
    float Zd = Bi[2][0]*lms[0] + Bi[2][1]*lms[1] + Bi[2][2]*lms[2];

    /* XYZ (D65) -> linear sRGB */
    float rl =  3.2406f*Xd + -1.5372f*Yd + -0.4986f*Zd;
    float gl = -0.9689f*Xd +  1.8758f*Yd +  0.0415f*Zd;
    float bl =  0.0557f*Xd + -0.2040f*Yd +  1.0570f*Zd;

    /* compand */
    out_rgb[0] = float_to_u8(srgb_compand(rl));
    out_rgb[1] = float_to_u8(srgb_compand(gl));
    out_rgb[2] = float_to_u8(srgb_compand(bl));
}

static psd_status_t render_planar_to_rgba8(
    psd_color_mode_t mode,
    uint16_t depth_bits,
    uint32_t width,
    uint32_t height,
    const uint8_t **planes,
    uint32_t plane_count,
    uint64_t plane_bytes,
    const uint8_t *color_mode_data,
    uint64_t color_mode_data_len,
    uint8_t *out_rgba,
    size_t out_rgba_size,
    size_t *out_required_size)
{
    uint64_t required64 = (uint64_t)width * (uint64_t)height * 4u;
    size_t required = 0;
    if (required64 > (uint64_t)SIZE_MAX) return PSD_ERR_OUT_OF_RANGE;
    required = (size_t)required64;
    if (out_required_size) *out_required_size = required;
    if (!out_rgba || out_rgba_size < required) return PSD_ERR_BUFFER_TOO_SMALL;

    if (width == 0 || height == 0) {
        return PSD_OK;
    }

    if (!(depth_bits == 1 || depth_bits == 8 || depth_bits == 16)) {
        return PSD_ERR_UNSUPPORTED_FEATURE;
    }

    const uint32_t bps = bytes_per_sample(depth_bits);
    const uint64_t row_bytes = (depth_bits == 1) ? (((uint64_t)width + 7u) / 8u) : 0;

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint8_t r = 0, g = 0, b = 0, a = 255;

            if (depth_bits == 1) {
                /* bitmap */
                if (!planes[0]) return PSD_ERR_CORRUPT_DATA;
                uint64_t off = (uint64_t)y * row_bytes + (uint64_t)(x / 8u);
                uint8_t bit = (uint8_t)(7u - (x & 7u));
                uint8_t v = ((planes[0][off] >> bit) & 1u) ? 255 : 0;
                r = g = b = v;
                a = 255;
            } else {
                uint64_t idx = (uint64_t)y * (uint64_t)width + (uint64_t)x;
                const uint8_t *p0 = planes[0] ? (planes[0] + idx * bps) : NULL;
                const uint8_t *p1 = (plane_count > 1 && planes[1]) ? (planes[1] + idx * bps) : NULL;
                const uint8_t *p2 = (plane_count > 2 && planes[2]) ? (planes[2] + idx * bps) : NULL;
                const uint8_t *p3 = (plane_count > 3 && planes[3]) ? (planes[3] + idx * bps) : NULL;
                const uint8_t *p4 = (plane_count > 4 && planes[4]) ? (planes[4] + idx * bps) : NULL;

                switch (mode) {
                case PSD_COLOR_RGB:
                    r = p0 ? sample_to_u8(p0, depth_bits) : 0;
                    g = p1 ? sample_to_u8(p1, depth_bits) : r;
                    b = p2 ? sample_to_u8(p2, depth_bits) : r;
                    a = p3 ? sample_to_u8(p3, depth_bits) : 255;
                    break;
                case PSD_COLOR_GRAYSCALE:
                case PSD_COLOR_DUOTONE:
                    r = p0 ? sample_to_u8(p0, depth_bits) : 0;
                    g = r;
                    b = r;
                    a = p1 ? sample_to_u8(p1, depth_bits) : 255;
                    break;
                case PSD_COLOR_INDEXED: {
                    uint8_t idx8 = p0 ? sample_to_u8(p0, depth_bits) : 0;
                    if (color_mode_data && color_mode_data_len >= 768) {
                        r = color_mode_data[idx8];
                        g = color_mode_data[256 + idx8];
                        b = color_mode_data[512 + idx8];
                    } else {
                        r = g = b = idx8;
                    }
                    a = p1 ? sample_to_u8(p1, depth_bits) : 255;
                    break;
                }
                case PSD_COLOR_CMYK: {
                    uint8_t c = p0 ? sample_to_u8(p0, depth_bits) : 0;
                    uint8_t m = p1 ? sample_to_u8(p1, depth_bits) : 0;
                    uint8_t yy = p2 ? sample_to_u8(p2, depth_bits) : 0;
                    uint8_t k = p3 ? sample_to_u8(p3, depth_bits) : 0;
                    uint16_t rk = (uint16_t)c + (uint16_t)k;
                    uint16_t gk = (uint16_t)m + (uint16_t)k;
                    uint16_t bk = (uint16_t)yy + (uint16_t)k;
                    r = (uint8_t)(255u - (rk > 255u ? 255u : rk));
                    g = (uint8_t)(255u - (gk > 255u ? 255u : gk));
                    b = (uint8_t)(255u - (bk > 255u ? 255u : bk));
                    a = p4 ? sample_to_u8(p4, depth_bits) : 255;
                    break;
                }
                case PSD_COLOR_LAB: {
                    if (!p0 || !p1 || !p2) {
                        return PSD_ERR_CORRUPT_DATA;
                    }
                    float L = 0.0f, aa = 0.0f, bb = 0.0f;
                    if (depth_bits == 8) {
                        uint8_t Lv = p0[0];
                        uint8_t av = p1[0];
                        uint8_t bv = p2[0];
                        L = ((float)Lv * 100.0f) / 255.0f;
                        aa = (float)((int)av - 128);
                        bb = (float)((int)bv - 128);
                    } else {
                        uint16_t Lv = read_be_u16(p0);
                        uint16_t av = read_be_u16(p1);
                        uint16_t bv = read_be_u16(p2);
                        L = ((float)Lv * 100.0f) / 65535.0f;
                        aa = ((float)((int)av - 32768)) / 256.0f;
                        bb = ((float)((int)bv - 32768)) / 256.0f;
                    }
                    uint8_t rgb[3];
                    lab_d50_to_srgb_u8(L, aa, bb, rgb);
                    r = rgb[0];
                    g = rgb[1];
                    b = rgb[2];
                    a = p3 ? sample_to_u8(p3, depth_bits) : 255;
                    break;
                }
                default:
                    return PSD_ERR_UNSUPPORTED_COLOR_MODE;
                }
            }

            size_t out_off = ((size_t)y * (size_t)width + (size_t)x) * 4u;
            out_rgba[out_off + 0] = r;
            out_rgba[out_off + 1] = g;
            out_rgba[out_off + 2] = b;
            out_rgba[out_off + 3] = a;
        }
    }

    (void)plane_bytes;
    return PSD_OK;
}

PSD_API psd_status_t psd_document_render_composite_rgba8(
    const psd_document_t *doc,
    uint8_t *out_rgba,
    size_t out_rgba_size,
    size_t *out_required_size)
{
    return psd_document_render_composite_rgba8_ex(
        doc, out_rgba, out_rgba_size, out_required_size, NULL);
}

PSD_API psd_status_t psd_document_render_composite_rgba8_ex(
    const psd_document_t *doc,
    uint8_t *out_rgba,
    size_t out_rgba_size,
    size_t *out_required_size,
    psd_render_composite_info_t *out_info)
{
    if (!doc) return PSD_ERR_NULL_POINTER;

    uint32_t width = 0, height = 0;
    psd_status_t st = psd_document_get_dimensions(doc, &width, &height);
    if (st != PSD_OK) return st;

    psd_color_mode_t mode = PSD_COLOR_RGB;
    st = psd_document_get_color_mode(doc, &mode);
    if (st != PSD_OK) return st;

    uint16_t depth_bits = 8;
    st = psd_document_get_depth(doc, &depth_bits);
    if (st != PSD_OK) return st;

    uint16_t channels = 0;
    st = psd_document_get_channels(doc, &channels);
    if (st != PSD_OK) return st;

    const uint8_t *composite = NULL;
    uint64_t composite_len = 0;
    uint32_t compression = 0;
    st = psd_document_get_composite_image(doc, &composite, &composite_len, &compression);
    if (st != PSD_OK) return st;

    if (out_info) {
        out_info->color_mode = mode;
        out_info->depth_bits = depth_bits;
        out_info->channels = channels;
        out_info->compression = compression;
    }

    if (!composite || composite_len == 0) {
        return PSD_ERR_INVALID_ARGUMENT;
    }

    uint32_t bps = bytes_per_sample(depth_bits);
    uint64_t plane_bytes = 0;
    if (depth_bits == 1) {
        plane_bytes = ((uint64_t)width + 7u) / 8u;
        plane_bytes *= (uint64_t)height;
    } else {
        if (bps == 0) return PSD_ERR_INVALID_ARGUMENT;
        plane_bytes = (uint64_t)width * (uint64_t)height * (uint64_t)bps;
    }

    if (channels == 0 || plane_bytes == 0) return PSD_ERR_CORRUPT_DATA;
    if (composite_len < (uint64_t)channels * plane_bytes) return PSD_ERR_CORRUPT_DATA;

    const uint8_t *cm_data = NULL;
    uint64_t cm_len = 0;
    psd_document_get_color_mode_data(doc, &cm_data, &cm_len);

    const uint8_t *planes[5] = { NULL, NULL, NULL, NULL, NULL };
    uint32_t plane_count = channels;
    if (plane_count > 5) plane_count = 5;
    for (uint32_t i = 0; i < plane_count; i++) {
        planes[i] = composite + (uint64_t)i * plane_bytes;
    }

    return render_planar_to_rgba8(
        mode, depth_bits, width, height,
        planes, plane_count, plane_bytes,
        cm_data, cm_len,
        out_rgba, out_rgba_size, out_required_size);
}

PSD_API psd_status_t psd_document_render_layer_rgba8(
    psd_document_t *doc,
    int32_t layer_index,
    uint8_t *out_rgba,
    size_t out_rgba_size,
    size_t *out_required_size)
{
    if (!doc) return PSD_ERR_NULL_POINTER;

    int32_t top = 0, left = 0, bottom = 0, right = 0;
    psd_status_t st = psd_document_get_layer_bounds(doc, layer_index, &top, &left, &bottom, &right);
    if (st != PSD_OK) return st;

    uint32_t width = (right > left) ? (uint32_t)(right - left) : 0;
    uint32_t height = (bottom > top) ? (uint32_t)(bottom - top) : 0;
    uint64_t required64 = (uint64_t)width * (uint64_t)height * 4u;
    if (required64 > (uint64_t)SIZE_MAX) return PSD_ERR_OUT_OF_RANGE;
    if (out_required_size) *out_required_size = (size_t)required64;
    if (!out_rgba || out_rgba_size < (size_t)required64) return PSD_ERR_BUFFER_TOO_SMALL;
    if (width == 0 || height == 0) return PSD_OK;

    psd_color_mode_t mode = PSD_COLOR_RGB;
    st = psd_document_get_color_mode(doc, &mode);
    if (st != PSD_OK) return st;

    uint16_t depth_bits = 8;
    st = psd_document_get_depth(doc, &depth_bits);
    if (st != PSD_OK) return st;

    const uint8_t *cm_data = NULL;
    uint64_t cm_len = 0;
    psd_document_get_color_mode_data(doc, &cm_data, &cm_len);

    size_t channel_count = 0;
    st = psd_document_get_layer_channel_count(doc, layer_index, &channel_count);
    if (st != PSD_OK) return st;

    const uint8_t *planes[5] = { NULL, NULL, NULL, NULL, NULL };
    uint32_t plane_count = 0;

    /* Find plane pointers by channel id. */
    for (size_t i = 0; i < channel_count; i++) {
        int16_t channel_id = 0;
        const uint8_t *data = NULL;
        uint64_t len = 0;
        uint32_t comp = 0;
        st = psd_document_get_layer_channel_data(doc, layer_index, i, &channel_id, &data, &len, &comp);
        if (st != PSD_OK) continue;
        (void)comp;

        if (!data || len == 0) continue;

        if (channel_id >= 0 && channel_id < 4) {
            planes[channel_id] = data;
        } else if (channel_id == -1) {
            planes[4] = data; /* store alpha temporarily in slot 4 */
        }
    }

    /* Build plane order expected by render_planar_to_rgba8:
     * p0.. base channels, then alpha as next plane when present.
     */
    const uint8_t *ordered[5] = { NULL, NULL, NULL, NULL, NULL };
    switch (mode) {
    case PSD_COLOR_RGB:
        ordered[0] = planes[0];
        ordered[1] = planes[1];
        ordered[2] = planes[2];
        if (planes[4]) ordered[3] = planes[4];
        plane_count = planes[4] ? 4 : 3;
        break;
    case PSD_COLOR_GRAYSCALE:
    case PSD_COLOR_DUOTONE:
    case PSD_COLOR_INDEXED:
        ordered[0] = planes[0];
        if (planes[4]) ordered[1] = planes[4];
        plane_count = planes[4] ? 2 : 1;
        break;
    case PSD_COLOR_CMYK:
        ordered[0] = planes[0];
        ordered[1] = planes[1];
        ordered[2] = planes[2];
        ordered[3] = planes[3];
        if (planes[4]) ordered[4] = planes[4];
        plane_count = planes[4] ? 5 : 4;
        break;
    case PSD_COLOR_LAB:
        ordered[0] = planes[0];
        ordered[1] = planes[1];
        ordered[2] = planes[2];
        if (planes[4]) ordered[3] = planes[4];
        plane_count = planes[4] ? 4 : 3;
        break;
    default:
        return PSD_ERR_UNSUPPORTED_COLOR_MODE;
    }

    uint32_t bps = bytes_per_sample(depth_bits);
    uint64_t plane_bytes = 0;
    if (depth_bits == 1) {
        plane_bytes = ((uint64_t)width + 7u) / 8u;
        plane_bytes *= (uint64_t)height;
    } else {
        if (bps == 0) return PSD_ERR_INVALID_ARGUMENT;
        plane_bytes = (uint64_t)width * (uint64_t)height * (uint64_t)bps;
    }

    return render_planar_to_rgba8(
        mode, depth_bits, width, height,
        ordered, plane_count, plane_bytes,
        cm_data, cm_len,
        out_rgba, out_rgba_size, out_required_size);
}

