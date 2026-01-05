/**
 * @file psd_stream.c
 * @brief Stream implementation
 *
 * Implements the stream interface for reading/writing data from various sources.
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

#include "../include/openpsd/psd_stream.h"
#include "../include/openpsd/psd_types.h"
#include "psd_alloc.h"
#include "psd_endian.h"
#include <string.h>
#include <stdint.h>
#include <limits.h>

/**
 * @brief Stream context
 */
struct psd_stream {
    psd_stream_vtable_t vtable;     /**< Virtual method table */
    void *user_data;                /**< User-defined context */
    const psd_allocator_t *allocator; /**< Allocator used for this stream */
};

/**
 * @brief Context for buffer-based stream
 */
typedef struct {
    const uint8_t *buffer;  /**< Buffer data */
    size_t length;          /**< Total buffer size */
    size_t position;        /**< Current read position */
} psd_buffer_stream_t;

/**
 * @brief Buffer stream read callback
 */
static int64_t psd_buffer_stream_read(
    psd_stream_t *stream,
    void *buffer,
    size_t count,
    void *user_data
)
{
    (void)stream; /* Unused */
    psd_buffer_stream_t *buf_stream = (psd_buffer_stream_t *)user_data;

    if (!buf_stream || !buffer) {
        return PSD_ERR_NULL_POINTER;
    }

    /* Calculate how much we can read */
    size_t remaining = buf_stream->length - buf_stream->position;
    size_t to_read = (count < remaining) ? count : remaining;

    if (to_read > 0) {
        memcpy(buffer, buf_stream->buffer + buf_stream->position, to_read);
        buf_stream->position += to_read;
    }

    return (int64_t)to_read;
}

/**
 * @brief Buffer stream write callback (not supported)
 */
static int64_t psd_buffer_stream_write(
    psd_stream_t *stream,
    const void *buffer,
    size_t count,
    void *user_data
)
{
    (void)stream;
    (void)buffer;
    (void)count;
    (void)user_data;
    return PSD_ERR_STREAM_INVALID; /* Read-only */
}

/**
 * @brief Buffer stream seek callback
 */
static int64_t psd_buffer_stream_seek(
    psd_stream_t *stream,
    int64_t offset,
    void *user_data
)
{
    (void)stream; /* Unused */
    psd_buffer_stream_t *buf_stream = (psd_buffer_stream_t *)user_data;

    if (!buf_stream) {
        return PSD_ERR_NULL_POINTER;
    }

    if (offset < 0 || offset > (int64_t)buf_stream->length) {
        return PSD_ERR_OUT_OF_RANGE;
    }

    buf_stream->position = (size_t)offset;
    return offset;
}

/**
 * @brief Buffer stream tell callback
 */
static int64_t psd_buffer_stream_tell(
    psd_stream_t *stream,
    void *user_data
)
{
    (void)stream; /* Unused */
    psd_buffer_stream_t *buf_stream = (psd_buffer_stream_t *)user_data;

    if (!buf_stream) {
        return PSD_ERR_NULL_POINTER;
    }

    return (int64_t)buf_stream->position;
}

/**
 * @brief Buffer stream close callback
 */
static psd_status_t psd_buffer_stream_close(
    psd_stream_t *stream,
    void *user_data
)
{
    (void)stream;
    (void)user_data;
    return PSD_OK; /* Nothing to clean up */
}

/**
 * @brief Virtual table for buffer streams
 */
static const psd_stream_vtable_t psd_buffer_vtable = {
    .read = psd_buffer_stream_read,
    .write = psd_buffer_stream_write,
    .seek = psd_buffer_stream_seek,
    .tell = psd_buffer_stream_tell,
    .close = psd_buffer_stream_close,
};

/**
 * @brief Create a stream from a buffer
 */
PSD_API psd_stream_t* psd_stream_create_buffer(
    const psd_allocator_t *allocator,
    const void *buffer,
    size_t length
)
{
    if (!buffer || length == 0) {
        return NULL;
    }

    /* Allocate stream structure */
    psd_stream_t *stream = (psd_stream_t *)psd_alloc_malloc(allocator, sizeof(*stream));
    if (!stream) {
        return NULL;
    }

    /* Allocate buffer stream context */
    psd_buffer_stream_t *buf_ctx = (psd_buffer_stream_t *)psd_alloc_malloc(
        allocator,
        sizeof(*buf_ctx)
    );
    if (!buf_ctx) {
        psd_alloc_free(allocator, stream);
        return NULL;
    }

    /* Initialize buffer stream */
    buf_ctx->buffer = (const uint8_t *)buffer;
    buf_ctx->length = length;
    buf_ctx->position = 0;

    /* Initialize stream */
    stream->vtable = psd_buffer_vtable;
    stream->user_data = buf_ctx;
    stream->allocator = allocator;

    return stream;
}

/**
 * @brief Create a custom stream
 */
PSD_API psd_stream_t* psd_stream_create_custom(
    const psd_allocator_t *allocator,
    const psd_stream_vtable_t *vtable,
    void *user_data
)
{
    if (!vtable || !vtable->read || !vtable->write || !vtable->seek || !vtable->tell) {
        return NULL;
    }

    psd_stream_t *stream = (psd_stream_t *)psd_alloc_malloc(allocator, sizeof(*stream));
    if (!stream) {
        return NULL;
    }

    stream->vtable = *vtable;
    stream->user_data = user_data;
    stream->allocator = allocator;

    return stream;
}

/**
 * @brief Destroy a stream
 */
PSD_API psd_status_t psd_stream_destroy(psd_stream_t *stream)
{
    if (!stream) {
        return PSD_OK;
    }

    const psd_allocator_t *allocator = stream->allocator;
    psd_status_t result = PSD_OK;

    /* Call close callback if present */
    if (stream->vtable.close) {
        result = stream->vtable.close(stream, stream->user_data);
    }

    /* Free the stream structure */
    psd_alloc_free(allocator, stream);

    return result;
}

/**
 * @brief Read from stream
 */
PSD_API int64_t psd_stream_read(
    psd_stream_t *stream,
    void *buffer,
    size_t count
)
{
    if (!stream || !buffer) {
        return PSD_ERR_NULL_POINTER;
    }

    return stream->vtable.read(stream, buffer, count, stream->user_data);
}

/**
 * @brief Write to stream
 */
PSD_API int64_t psd_stream_write(
    psd_stream_t *stream,
    const void *buffer,
    size_t count
)
{
    if (!stream || !buffer) {
        return PSD_ERR_NULL_POINTER;
    }

    return stream->vtable.write(stream, buffer, count, stream->user_data);
}

/**
 * @brief Seek in stream
 */
PSD_API int64_t psd_stream_seek(psd_stream_t *stream, int64_t offset)
{
    if (!stream) {
        return PSD_ERR_NULL_POINTER;
    }

    return stream->vtable.seek(stream, offset, stream->user_data);
}

/**
 * @brief Get current position
 */
PSD_API int64_t psd_stream_tell(psd_stream_t *stream)
{
    if (!stream) {
        return PSD_ERR_NULL_POINTER;
    }

    return stream->vtable.tell(stream, stream->user_data);
}

/**
 * @brief Read exactly count bytes
 */
PSD_API psd_status_t psd_stream_read_exact(
    psd_stream_t *stream,
    void *buffer,
    size_t count
)
{
    if (!stream || !buffer) {
        return PSD_ERR_NULL_POINTER;
    }

    size_t read_total = 0;
    while (read_total < count) {
        int64_t result = psd_stream_read(stream, (uint8_t *)buffer + read_total, count - read_total);
        if (result < 0) {
            return (psd_status_t)result;
        }
        if (result == 0) {
            return PSD_ERR_STREAM_EOF;
        }
        read_total += (size_t)result;
    }

    return PSD_OK;
}

/**
 * @brief Read big-endian 16-bit integer
 */
PSD_API psd_status_t psd_stream_read_be16(psd_stream_t *stream, uint16_t *value)
{
    if (!stream || !value) {
        return PSD_ERR_NULL_POINTER;
    }

    uint8_t buffer[2];
    psd_status_t status = psd_stream_read_exact(stream, buffer, sizeof(buffer));
    if (status != PSD_OK) {
        return status;
    }

    *value = psd_read_be16(buffer);
    return PSD_OK;
}

/**
 * @brief Read big-endian 32-bit integer
 */
PSD_API psd_status_t psd_stream_read_be32(psd_stream_t *stream, uint32_t *value)
{
    if (!stream || !value) {
        return PSD_ERR_NULL_POINTER;
    }

    uint8_t buffer[4];
    psd_status_t status = psd_stream_read_exact(stream, buffer, sizeof(buffer));
    if (status != PSD_OK) {
        return status;
    }

    *value = psd_read_be32(buffer);
    return PSD_OK;
}

/**
 * @brief Read big-endian 64-bit integer
 */
PSD_API psd_status_t psd_stream_read_be64(psd_stream_t *stream, uint64_t *value)
{
    if (!stream || !value) {
        return PSD_ERR_NULL_POINTER;
    }

    uint8_t buffer[8];
    psd_status_t status = psd_stream_read_exact(stream, buffer, sizeof(buffer));
    if (status != PSD_OK) {
        return status;
    }

    *value = psd_read_be64(buffer);
    return PSD_OK;
}

/**
 * @brief Skip forward in stream
 */
PSD_API psd_status_t psd_stream_skip(psd_stream_t *stream, size_t count)
{
    if (!stream) {
        return PSD_ERR_NULL_POINTER;
    }

    if (count == 0) {
        return PSD_OK;
    }

    /* Use a fixed-size buffer to skip data */
    #define SKIP_BUFFER_SIZE 4096
    uint8_t buffer[SKIP_BUFFER_SIZE];

    size_t skipped = 0;
    while (skipped < count) {
        size_t to_skip = count - skipped;
        if (to_skip > SKIP_BUFFER_SIZE) {
            to_skip = SKIP_BUFFER_SIZE;
        }

        psd_status_t status = psd_stream_read_exact(stream, buffer, to_skip);
        if (status != PSD_OK) {
            return status;
        }

        skipped += to_skip;
    }

    return PSD_OK;

    #undef SKIP_BUFFER_SIZE
}

/**
 * @brief Read big-endian signed 32-bit integer
 */
PSD_API psd_status_t psd_stream_read_be_i32(psd_stream_t *stream, int32_t *value)
{
    if (!stream || !value) {
        return PSD_ERR_NULL_POINTER;
    }

    uint8_t buffer[4];
    psd_status_t status = psd_stream_read_exact(stream, buffer, sizeof(buffer));
    if (status != PSD_OK) {
        return status;
    }

    *value = psd_read_be_i32(buffer);
    return PSD_OK;
}

/**
 * @brief Read 32-bit or 64-bit length value
 */
PSD_API psd_status_t psd_stream_read_length(
    psd_stream_t *stream,
    bool is_psb,
    uint64_t *out
)
{
    if (!stream || !out) {
        return PSD_ERR_NULL_POINTER;
    }

    if (is_psb) {
        /* PSB format: 64-bit length */
        uint64_t value64;
        psd_status_t status = psd_stream_read_be64(stream, &value64);
        if (status != PSD_OK) {
            return status;
        }

        /* Check overflow: value must fit in size_t */
        size_t dummy;
        if (psd_u64_to_size(value64, &dummy) != 0) {
            return PSD_ERR_OUT_OF_RANGE;
        }

        *out = value64;
        return PSD_OK;
    } else {
        /* Standard PSD format: 32-bit length */
        uint32_t value32;
        psd_status_t status = psd_stream_read_be32(stream, &value32);
        if (status != PSD_OK) {
            return status;
        }

        *out = (uint64_t)value32;
        return PSD_OK;
    }
}
