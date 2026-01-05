/**
 * @file psd_alloc.h
 * @brief Internal allocation utilities
 *
 * Internal header with allocation helper functions.
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

#ifndef PSD_ALLOC_H
#define PSD_ALLOC_H

#include "../include/openpsd/psd_types.h"
#include "../include/openpsd/psd_export.h"

/**
 * @brief Get default allocator
 *
 * @return Pointer to default allocator
 */
PSD_INTERNAL const psd_allocator_t* psd_allocator_default(void);

/**
 * @brief Allocate memory
 *
 * @param allocator Allocator (NULL for default)
 * @param size Bytes to allocate
 * @return Allocated memory or NULL
 */
PSD_INTERNAL void* psd_alloc_malloc(const psd_allocator_t *allocator, size_t size);

/**
 * @brief Reallocate memory
 *
 * @param allocator Allocator (NULL for default)
 * @param ptr Previous allocation
 * @param size New size
 * @return Reallocated memory or NULL
 */
PSD_INTERNAL void* psd_alloc_realloc(const psd_allocator_t *allocator, void *ptr, size_t size);

/**
 * @brief Free memory
 *
 * @param allocator Allocator (NULL for default)
 * @param ptr Memory to free
 */
PSD_INTERNAL void psd_alloc_free(const psd_allocator_t *allocator, void *ptr);

#endif /* PSD_ALLOC_H */
