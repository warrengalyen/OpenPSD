/**
 * @file psd_parse.c
 * @brief Main parsing logic
 *
 * Implements the core parsing functionality for PSD files.
 * Parsing is done on-demand without storing unnecessary data.
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

#include "../include/openpsd/psd.h"
#include "../include/openpsd/psd_error.h"

/**
 * @brief Error message strings
 *
 * Maps error codes to human-readable messages.
 */
static const char* psd_error_messages[] = {
    /* Success */
    [PSD_OK] = "Operation successful",

    /* General errors */
    [-PSD_ERR_INVALID_ARGUMENT] = "Invalid argument to function",
    [-PSD_ERR_OUT_OF_MEMORY] = "Memory allocation failed",
    [-PSD_ERR_NULL_POINTER] = "Unexpected NULL pointer",
    [-PSD_ERR_NOT_INITIALIZED] = "Object not properly initialized",
    [-PSD_ERR_ALREADY_INITIALIZED] = "Object already initialized",

    /* Stream/IO errors */
    [-PSD_ERR_STREAM_READ] = "Failed to read from stream",
    [-PSD_ERR_STREAM_WRITE] = "Failed to write to stream",
    [-PSD_ERR_STREAM_SEEK] = "Failed to seek in stream",
    [-PSD_ERR_STREAM_INVALID] = "Invalid stream state",
    [-PSD_ERR_STREAM_EOF] = "Unexpected end of stream",

    /* Format errors */
    [-PSD_ERR_INVALID_FILE_FORMAT] = "File is not a valid PSD",
    [-PSD_ERR_INVALID_HEADER] = "Invalid PSD header",
    [-PSD_ERR_UNSUPPORTED_VERSION] = "PSD version not supported",
    [-PSD_ERR_CORRUPT_DATA] = "Corrupted data encountered",
    [-PSD_ERR_INVALID_STRUCTURE] = "Invalid data structure",

    /* Feature support errors */
    [-PSD_ERR_UNSUPPORTED_FEATURE] = "Feature not yet implemented",
    [-PSD_ERR_UNSUPPORTED_COMPRESSION] = "Compression not supported",
    [-PSD_ERR_UNSUPPORTED_COLOR_MODE] = "Color mode not supported",

    /* Range errors */
    [-PSD_ERR_BUFFER_TOO_SMALL] = "Buffer too small for operation",
    [-PSD_ERR_OUT_OF_RANGE] = "Value out of valid range",
};

/**
 * @brief Get error string for error code
 */
PSD_API const char* psd_error_string(psd_status_t status)
{
    if (status == PSD_OK) {
        return "Operation successful";
    }

    if (status < 0) {
        int index = -status;
        if (index >= 0 && index < (int)(sizeof(psd_error_messages) / sizeof(psd_error_messages[0]))) {
            const char *msg = psd_error_messages[index];
            if (msg) {
                return msg;
            }
        }
    }

    return "Unknown error";
}

/**
 * @brief Get library version string
 */
PSD_API const char* psd_get_version(void)
{
    return "0.1.0";
}

/**
 * @brief Get library version string (deprecated, for backward compatibility)
 */
PSD_API const char* psd_version(void)
{
    return psd_get_version();
}

/**
 * @brief Get version components
 */
PSD_API void psd_version_components(
    int *major,
    int *minor,
    int *patch
)
{
    if (major) {
        *major = PSD_VERSION_MAJOR;
    }
    if (minor) {
        *minor = PSD_VERSION_MINOR;
    }
    if (patch) {
        *patch = PSD_VERSION_PATCH;
    }
}
