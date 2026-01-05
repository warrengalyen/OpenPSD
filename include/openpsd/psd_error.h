/**
 * @file psd_error.h
 * @brief Error codes and status reporting
 *
 * Defines all error codes and status values used by the library.
 * All library functions return a psd_status_t value to indicate success or failure.
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

#ifndef PSD_ERROR_H
#define PSD_ERROR_H

#include <stdint.h>
#include "psd_export.h"

/**
 * @typedef psd_status_t
 * @brief Status code returned by library functions
 *
 * Functions return PSD_OK (0) on success, or a negative error code on failure.
 * Error codes follow the convention:
 * - 0 or positive: Success
 * - Negative: Error
 */
typedef int32_t psd_status_t;

/**
 * @enum psd_error_codes
 * @brief Error codes returned by library functions
 */
typedef enum {
    /* Success codes */
    PSD_OK = 0,                          /**< Operation successful */

    /* General errors (-1 to -99) */
    PSD_ERR_INVALID_ARGUMENT = -1,       /**< Invalid argument to function */
    PSD_ERR_OUT_OF_MEMORY = -2,          /**< Memory allocation failed */
    PSD_ERR_NULL_POINTER = -3,           /**< Unexpected NULL pointer */
    PSD_ERR_NOT_INITIALIZED = -4,        /**< Object not properly initialized */
    PSD_ERR_ALREADY_INITIALIZED = -5,    /**< Object already initialized */
    PSD_ERR_INVALID_FORMAT = -6,         /**< Invalid data format */

    /* Stream/IO errors (-100 to -199) */
    PSD_ERR_STREAM_READ = -100,          /**< Failed to read from stream */
    PSD_ERR_STREAM_WRITE = -101,         /**< Failed to write to stream */
    PSD_ERR_STREAM_SEEK = -102,          /**< Failed to seek in stream */
    PSD_ERR_STREAM_INVALID = -103,       /**< Invalid stream state */
    PSD_ERR_STREAM_EOF = -104,           /**< Unexpected end of stream */

    /* Format errors (-200 to -299) */
    PSD_ERR_INVALID_FILE_FORMAT = -200,  /**< File is not a valid PSD */
    PSD_ERR_INVALID_HEADER = -201,       /**< Invalid PSD header */
    PSD_ERR_UNSUPPORTED_VERSION = -202,  /**< PSD version not supported */
    PSD_ERR_CORRUPT_DATA = -203,         /**< Corrupted data encountered */
    PSD_ERR_INVALID_STRUCTURE = -204,    /**< Invalid data structure */

    /* Feature support errors (-300 to -399) */
    PSD_ERR_UNSUPPORTED_FEATURE = -300,  /**< Feature not yet implemented */
    PSD_ERR_UNSUPPORTED_COMPRESSION = -301, /**< Compression not supported */
    PSD_ERR_UNSUPPORTED_COLOR_MODE = -302,  /**< Color mode not supported */

    /* Range errors (-400 to -499) */
    PSD_ERR_BUFFER_TOO_SMALL = -400,     /**< Buffer too small for operation */
    PSD_ERR_OUT_OF_RANGE = -401,         /**< Value out of valid range */

} psd_error_codes;

/**
 * @brief Get human-readable error message
 *
 * @param status Error code to describe
 * @return Pointer to static error message string (never NULL)
 */
PSD_API const char* psd_error_string(psd_status_t status);

#endif /* PSD_ERROR_H */
