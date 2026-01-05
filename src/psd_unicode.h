/**
 * @file psd_unicode.h
 * @brief Internal unicode utilities
 *
 * Internal header with unicode conversion functions.
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

#ifndef PSD_UNICODE_H
#define PSD_UNICODE_H

#include <stdint.h>
#include "psd_alloc.h"
#include "../include/openpsd/psd_error.h"
#include "../include/openpsd/psd_export.h"

/**
 * @brief Encode a UTF-16 character to UTF-8
 *
 * @param cp Pointer to the UTF-16 character
 * @param out Pointer to the output buffer
 * @return The number of bytes written to the output buffer
 */
PSD_INTERNAL size_t psd_utf8_encode(uint32_t cp, uint8_t out[4]);

/**
 * @brief Convert MacRoman bytes to UTF-8
 *
 * @param alloc Allocator to use
 * @param in Pointer to the input buffer
 * @param in_len The number of bytes in the input buffer
 * @param out_len Pointer to the length of the output buffer
 * @return The converted UTF-8 buffer
 */
PSD_INTERNAL uint8_t *psd_macroman_to_utf8(const psd_allocator_t *alloc,
                                            const uint8_t *in, size_t in_len,
                                            size_t *out_len);

/**
 * @brief Convert UTF-16BE bytes to UTF-8
 *
 * @param alloc Allocator to use
 * @param in Pointer to the input buffer
 * @param in_bytes The number of bytes in the input buffer
 * @param out_len Pointer to the length of the output buffer
 * @return The converted UTF-8 buffer
 */
PSD_INTERNAL uint8_t *psd_utf16be_to_utf8(const psd_allocator_t *alloc,
                                const uint8_t *in,
                                size_t in_bytes,
                                size_t *out_len);

#endif /* PSD_UNICODE_H */