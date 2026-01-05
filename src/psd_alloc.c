/**
 * @file psd_alloc.c
 * @brief Memory allocation utilities
 * 
 * Provides default memory allocator and utilities for custom allocators.
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

#include "../include/openpsd/psd_types.h"
#include <stdlib.h>

/**
 * @brief Default malloc implementation
 */
static void* psd_malloc_default(size_t size, void *user_data)
{
    (void)user_data; /* Unused */
    return malloc(size);
}

/**
 * @brief Default realloc implementation
 */
static void* psd_realloc_default(void *ptr, size_t size, void *user_data)
{
    (void)user_data; /* Unused */
    return realloc(ptr, size);
}

/**
 * @brief Default free implementation
 */
static void psd_free_default(void *ptr, void *user_data)
{
    (void)user_data; /* Unused */
    free(ptr);
}

/**
 * @brief Get default allocator
 *
 * Returns a static allocator that uses the standard C library
 * malloc/realloc/free functions.
 *
 * @return Pointer to default allocator (never NULL)
 */
const psd_allocator_t* psd_allocator_default(void)
{
    static const psd_allocator_t default_allocator = {
        .malloc = psd_malloc_default,
        .realloc = psd_realloc_default,
        .free = psd_free_default,
        .user_data = NULL,
    };
    return &default_allocator;
}

/**
 * @brief Allocate memory using allocator
 *
 * Helper function that handles NULL allocator by using the default.
 *
 * @param allocator Allocator to use (NULL for default)
 * @param size Bytes to allocate
 * @return Allocated memory or NULL on failure
 */
void* psd_alloc_malloc(const psd_allocator_t *allocator, size_t size)
{
    if (!allocator) {
        allocator = psd_allocator_default();
    }
    if (!allocator->malloc) {
        return NULL;
    }
    return allocator->malloc(size, allocator->user_data);
}

/**
 * @brief Reallocate memory using allocator
 *
 * Helper function that handles NULL allocator by using the default.
 *
 * @param allocator Allocator to use (NULL for default)
 * @param ptr Previous allocation
 * @param size New size
 * @return Reallocated memory or NULL on failure
 */
void* psd_alloc_realloc(const psd_allocator_t *allocator, void *ptr, size_t size)
{
    if (!allocator) {
        allocator = psd_allocator_default();
    }
    if (!allocator->realloc) {
        return NULL;
    }
    return allocator->realloc(ptr, size, allocator->user_data);
}

/**
 * @brief Free memory using allocator
 *
 * Helper function that handles NULL allocator by using the default.
 *
 * @param allocator Allocator to use (NULL for default)
 * @param ptr Memory to free
 */
void psd_alloc_free(const psd_allocator_t *allocator, void *ptr)
{
    if (!ptr) {
        return; /* Safe to free NULL */
    }
    if (!allocator) {
        allocator = psd_allocator_default();
    }
    if (allocator->free) {
        allocator->free(ptr, allocator->user_data);
    }
}
