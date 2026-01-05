/**
 * @file psd_export.h
 * @brief Symbol visibility and ABI  macros
 *
 * Defines the PSD_API macro for proper symbol export/import across
 * Windows (dllexport/dllimport) and Unix-like systems (visibility attributes).
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

#ifndef PSD_EXPORT_H
#define PSD_EXPORT_H

/**
 * @def PSD_API
 * @brief Export/import macro for public API symbols
 *
 * On Windows, expands to __declspec(dllexport) for building the library
 * and __declspec(dllimport) for consuming applications.
 *
 * On GCC/Clang, uses __attribute__((visibility("default"))) for explicit
 * symbol visibility with -fvisibility=hidden compilation flag.
 *
 * On other compilers, expands to empty (default visibility).
 */

/**
 * @def PSD_INTERNAL
 * @brief Mark symbols as internal/hidden
 *
 * Used for internal helper functions that should not be exported.
 * For shared libraries, these are explicitly hidden.
 * For static libraries, they still appear in the symbol table but are not
 * part of the public ABI.
 */

/* Windows-specific handling */
#if defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__)
  #if defined(PSD_BUILDING_SHARED_LIB)
    /* Building the shared library - export symbols */
    #define PSD_API __declspec(dllexport)
    #define PSD_INTERNAL __declspec(dllexport)
  #elif defined(PSD_SHARED_LIB)
    /* Using the shared library - import symbols */
    #define PSD_API __declspec(dllimport)
    #define PSD_INTERNAL
  #else
    /* Static library - no special handling needed */
    #define PSD_API
    #define PSD_INTERNAL
  #endif
#else
  /* Unix-like systems (Linux, macOS, etc.) */
  #if defined(__GNUC__) || defined(__clang__)
    #if defined(PSD_BUILDING_SHARED_LIB)
      /* Building with visibility control */
      #define PSD_API __attribute__((visibility("default")))
      #define PSD_INTERNAL __attribute__((visibility("hidden")))
    #else
      #define PSD_API
      #define PSD_INTERNAL
    #endif
  #else
    /* Other compilers - default visibility */
    #define PSD_API
    #define PSD_INTERNAL
  #endif
#endif

#endif /* PSD_EXPORT_H */
