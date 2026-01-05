/**
 * @file psd_stream.h
 * @brief Stream I/O interface for reading and writing data
 *
 * Provides an abstract interface for stream operations, allowing the library
 * to work with any data source (memory buffers, files, network, etc.) without
 * depending on FILE* pointers.
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

#ifndef PSD_STREAM_H
#define PSD_STREAM_H

#include "psd_export.h"
#include "psd_error.h"
#include "psd_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Opaque stream context
 */
typedef struct psd_stream psd_stream_t;

/**
 * @brief Read callback for custom stream implementations
 *
 * @param stream Stream context
 * @param buffer Where to store read data
 * @param count Number of bytes to read
 * @param user_data User-defined context
 * @return Number of bytes actually read, or negative error code
 */
typedef int64_t (*psd_stream_read_fn)(
    psd_stream_t *stream,
    void *buffer,
    size_t count,
    void *user_data
);

/**
 * @brief Write callback for custom stream implementations
 *
 * @param stream Stream context
 * @param buffer Data to write
 * @param count Number of bytes to write
 * @param user_data User-defined context
 * @return Number of bytes actually written, or negative error code
 */
typedef int64_t (*psd_stream_write_fn)(
    psd_stream_t *stream,
    const void *buffer,
    size_t count,
    void *user_data
);

/**
 * @brief Seek callback for custom stream implementations
 *
 * @param stream Stream context
 * @param offset New absolute position
 * @param user_data User-defined context
 * @return New position on success, negative error code on failure
 */
typedef int64_t (*psd_stream_seek_fn)(
    psd_stream_t *stream,
    int64_t offset,
    void *user_data
);

/**
 * @brief Tell callback to get current position
 *
 * @param stream Stream context
 * @param user_data User-defined context
 * @return Current position on success, negative error code on failure
 */
typedef int64_t (*psd_stream_tell_fn)(
    psd_stream_t *stream,
    void *user_data
);

/**
 * @brief Close/cleanup callback
 *
 * @param stream Stream context
 * @param user_data User-defined context
 * @return PSD_OK on success, negative error code on failure
 */
typedef psd_status_t (*psd_stream_close_fn)(
    psd_stream_t *stream,
    void *user_data
);

/**
 * @brief Virtual method table for stream operations
 *
 * All function pointers except close are required.
 * Close is optional (can be NULL for streams that don't need cleanup).
 */
typedef struct {
    psd_stream_read_fn read;     /**< Read data from stream (required) */
    psd_stream_write_fn write;   /**< Write data to stream (required) */
    psd_stream_seek_fn seek;     /**< Seek to position (required) */
    psd_stream_tell_fn tell;     /**< Get current position (required) */
    psd_stream_close_fn close;   /**< Cleanup (optional) */
} psd_stream_vtable_t;

/**
 * @brief Create a stream from a read-only buffer
 *
 * Creates a stream that reads from a fixed-size memory buffer.
 * The buffer must remain valid for the lifetime of the stream.
 *
 * @param allocator Memory allocator (can be NULL for default)
 * @param buffer Buffer to read from
 * @param length Size of buffer
 * @return New stream on success, NULL on failure
 */
PSD_API psd_stream_t* psd_stream_create_buffer(
    const psd_allocator_t *allocator,
    const void *buffer,
    size_t length
);

/**
 * @brief Create a stream from custom operations
 *
 * Creates a stream using custom callback functions.
 * Useful for reading from files, network sockets, or other sources.
 *
 * @param allocator Memory allocator (can be NULL for default)
 * @param vtable Virtual method table with callbacks
 * @param user_data Context passed to callbacks
 * @return New stream on success, NULL on failure
 */
PSD_API psd_stream_t* psd_stream_create_custom(
    const psd_allocator_t *allocator,
    const psd_stream_vtable_t *vtable,
    void *user_data
);

/**
 * @brief Destroy a stream
 *
 * Calls the close callback if present, then frees the stream structure.
 *
 * @param stream Stream to destroy (safe if NULL)
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_stream_destroy(psd_stream_t *stream);

/**
 * @brief Read from stream
 *
 * Reads up to count bytes from the current position.
 *
 * @param stream Stream to read from
 * @param buffer Where to store data
 * @param count Number of bytes to read
 * @return Number of bytes read on success, negative error code on failure
 */
PSD_API int64_t psd_stream_read(
    psd_stream_t *stream,
    void *buffer,
    size_t count
);

/**
 * @brief Write to stream
 *
 * Writes count bytes starting from the current position.
 *
 * @param stream Stream to write to
 * @param buffer Data to write
 * @param count Number of bytes to write
 * @return Number of bytes written on success, negative error code on failure
 */
PSD_API int64_t psd_stream_write(
    psd_stream_t *stream,
    const void *buffer,
    size_t count
);

/**
 * @brief Seek to absolute position
 *
 * @param stream Stream to seek in
 * @param offset Absolute position from start of stream
 * @return New position on success, negative error code on failure
 */
PSD_API int64_t psd_stream_seek(psd_stream_t *stream, int64_t offset);

/**
 * @brief Get current position
 *
 * @param stream Stream to query
 * @return Current position on success, negative error code on failure
 */
PSD_API int64_t psd_stream_tell(psd_stream_t *stream);

/**
 * @brief Read exactly the specified number of bytes
 *
 * Unlike psd_stream_read, this function reads exactly count bytes
 * or returns an error if EOF is reached.
 *
 * @param stream Stream to read from
 * @param buffer Where to store data
 * @param count Number of bytes to read
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_stream_read_exact(
    psd_stream_t *stream,
    void *buffer,
    size_t count
);

/**
 * @brief Read a big-endian 16-bit integer
 *
 * @param stream Stream to read from
 * @param value Where to store the value
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_stream_read_be16(psd_stream_t *stream, uint16_t *value);

/**
 * @brief Read a big-endian 32-bit integer
 *
 * @param stream Stream to read from
 * @param value Where to store the value
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_stream_read_be32(psd_stream_t *stream, uint32_t *value);

/**
 * @brief Read a big-endian 64-bit integer
 *
 * @param stream Stream to read from
 * @param value Where to store the value
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_stream_read_be64(psd_stream_t *stream, uint64_t *value);

/**
 * @brief Skip forward in the stream
 *
 * Advances the stream position by count bytes. Reads and discards the data.
 * This is useful for skipping over data sections without storing them.
 *
 * @param stream Stream to skip in
 * @param count Number of bytes to skip
 * @return PSD_OK on success, negative error code on failure (including EOF)
 */
PSD_API psd_status_t psd_stream_skip(psd_stream_t *stream, size_t count);

/**
 * @brief Read a big-endian signed 32-bit integer
 *
 * @param stream Stream to read from
 * @param value Where to store the value
 * @return PSD_OK on success, negative error code on failure
 */
PSD_API psd_status_t psd_stream_read_be_i32(psd_stream_t *stream, int32_t *value);

/**
 * @brief Read a 32-bit or 64-bit length value
 *
 * PSD files use 32-bit lengths in standard PSD and 64-bit in PSB (large document) format.
 * This function reads the appropriate size and ensures no overflow when converting to size_t.
 *
 * @param stream Stream to read from
 * @param is_psb If true, reads 64-bit; if false, reads 32-bit
 * @param out Where to store the length as uint64_t
 * @return PSD_OK on success, PSD_ERR_OUT_OF_RANGE if value would overflow size_t, or other error code
 */
PSD_API psd_status_t psd_stream_read_length(
    psd_stream_t *stream,
    bool is_psb,
    uint64_t *out
);

#endif /* PSD_STREAM_H */
