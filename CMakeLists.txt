cmake_minimum_required(VERSION 3.15)
project(openpsd
    VERSION 0.1.0
    DESCRIPTION "Pure C library for parsing Adobe Photoshop PSD/PSB files"
    HOMEPAGE_URL "https://github.com/warrengalyen/openpsd"
    LANGUAGES C
)

# ============================================================================
# Enforce C17 standard
# ============================================================================

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ============================================================================
# Build options
# ============================================================================

option(BUILD_SHARED_LIBS "Build shared libraries instead of static" OFF)
option(BUILD_TESTS "Build test suite" ON)
option(OPENPSD_ENABLE_ZIP "Enable ZIP/zlib compression support" ON)
option(OPENPSD_TEXT_LAYER_DEBUG "Enable text layer debug logging" OFF)

# ============================================================================
# Set default visibility to hidden (for symbol control on Unix-like systems)
# ============================================================================

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# ============================================================================
# Optional dependencies
# ============================================================================

if(OPENPSD_ENABLE_ZIP)
    # Try pkg-config first (works well on MSYS2)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(ZLIB_PKG QUIET zlib)
        if(ZLIB_PKG_FOUND)
            message(STATUS "zlib found via pkg-config: ${ZLIB_PKG_LIBRARIES}")
            set(ZLIB_INCLUDE_DIRS ${ZLIB_PKG_INCLUDE_DIRS})
            set(ZLIB_LIBRARIES ${ZLIB_PKG_LIBRARIES})
            set(ZLIB_FOUND TRUE)
        endif()
    endif()
    
    # If pkg-config didn't find it, try CMake's find_package
    if(NOT ZLIB_FOUND)
        # Add common MSYS2 paths to search
        if(EXISTS "C:/msys64/mingw64")
            list(APPEND CMAKE_PREFIX_PATH "C:/msys64/mingw64")
        endif()
        if(EXISTS "C:/msys64/ucrt64")
            list(APPEND CMAKE_PREFIX_PATH "C:/msys64/ucrt64")
        endif()
        if(EXISTS "/mingw64")
            list(APPEND CMAKE_PREFIX_PATH "/mingw64")
        endif()
        if(EXISTS "/ucrt64")
            list(APPEND CMAKE_PREFIX_PATH "/ucrt64")
        endif()
        
        find_package(ZLIB)
    endif()
    
    if(ZLIB_FOUND)
        message(STATUS "zlib found: ${ZLIB_LIBRARIES}")
        set(ZLIB_STATUS "Enabled (found)")
    else()
        message(STATUS "zlib not found - ZIP compression support disabled")
        message(STATUS "  Hint: Install with: pacman -S mingw-w64-x86_64-zlib (or mingw-w64-ucrt-x86_64-zlib)")
        set(OPENPSD_ENABLE_ZIP OFF)
        set(ZLIB_STATUS "Disabled (not found)")
    endif()
else()
    set(ZLIB_STATUS "Disabled (option OFF)")
endif()

# ============================================================================
# Source files
# ============================================================================

set(OPENPSD_SOURCES
    src/psd_parse.c
    src/psd_stream.c
    src/psd_endian.c
    src/psd_context.c
    src/psd_alloc.c
    src/psd_rle.c
    src/psd_layer_decode.c
    src/psd_descriptor.c
    src/psd_unicode.c
    src/psd_zip.c
    src/psd_render.c
    src/psd_text_layer.c
    src/psd_text_layer_parse.c
)

set(OPENPSD_HEADERS
    include/openpsd/psd.h
    include/openpsd/psd_types.h
    include/openpsd/psd_error.h
    include/openpsd/psd_stream.h
    include/openpsd/psd_export.h
)

# ============================================================================
# Create library (static or shared based on BUILD_SHARED_LIBS)
# ============================================================================

add_library(openpsd ${OPENPSD_SOURCES})

# Set output names to avoid conflicts
if(BUILD_SHARED_LIBS)
    set_target_properties(openpsd PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
else()
    set_target_properties(openpsd PROPERTIES
        VERSION ${PROJECT_VERSION}
    )
endif()

# ============================================================================
# Public interface
# ============================================================================

target_include_directories(openpsd
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# ============================================================================
# Symbol visibility setup
# ============================================================================

# When building the shared library, define PSD_BUILDING_SHARED_LIB
target_compile_definitions(openpsd
    PRIVATE
        $<$<BOOL:${BUILD_SHARED_LIBS}>:PSD_BUILDING_SHARED_LIB>
)

# ZIP support configuration
if(OPENPSD_ENABLE_ZIP)
    target_compile_definitions(openpsd PRIVATE PSD_ENABLE_ZIP)
    if(ZLIB_FOUND)
        # Use pkg-config result if available, otherwise use find_package result
        if(ZLIB_PKG_FOUND)
            target_link_libraries(openpsd PRIVATE ${ZLIB_PKG_LIBRARIES})
            target_include_directories(openpsd PRIVATE ${ZLIB_PKG_INCLUDE_DIRS})
        else()
            # Use CMake's find_package result (provides ZLIB::ZLIB target)
            target_link_libraries(openpsd PRIVATE ZLIB::ZLIB)
            target_include_directories(openpsd PRIVATE ${ZLIB_INCLUDE_DIRS})
        endif()
    endif()
endif()

# When consuming the shared library, define PSD_SHARED_LIB
# This is handled in installed CMake config files below

# ============================================================================
# Compiler-specific options
# ============================================================================

# MSVC
if(MSVC)
    target_compile_options(openpsd PRIVATE
        /W4          # Warning level 4
        /WX          # Treat warnings as errors
        /TC          # Compile as C (not C++)
    )
endif()

# GCC/Clang
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(openpsd PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wstrict-prototypes
        -Wwrite-strings
        -Wshadow
        -Wundef
        -fPIC
    )

    # psd_render.c uses math functions (powf/lroundf)
    target_link_libraries(openpsd PRIVATE m)
endif()

# ============================================================================
# Installation setup
# ============================================================================

# Install headers
install(DIRECTORY include/openpsd
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# Install library
install(TARGETS openpsd
    EXPORT openpsdTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

# ============================================================================
# Generate CMake package files
# ============================================================================

include(CMakePackageConfigHelpers)

# Generate version file
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/openpsdConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Generate config file
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/openpsdConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/openpsdConfig.cmake"
    @ONLY
)

# Install CMake config files
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/openpsdConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/openpsdConfigVersion.cmake"
    DESTINATION lib/cmake/openpsd
)

# Install targets file
install(EXPORT openpsdTargets
    FILE openpsdTargets.cmake
    NAMESPACE openpsd::
    DESTINATION lib/cmake/openpsd
)

# ============================================================================
# Build tests if enabled
# ============================================================================

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================================
# Build GUI app (optional, requires GTK and Cairo)
# ============================================================================
add_subdirectory(demo)

# ============================================================================
# Print configuration summary
# ============================================================================

message(STATUS "")
message(STATUS "openpsd configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Library type: ${LIBRARY_TYPE}")
message(STATUS "  Build shared libs: ${BUILD_SHARED_LIBS}")
message(STATUS "  Build tests: ${BUILD_TESTS}")
message(STATUS "  ZIP compression support: ${ZLIB_STATUS}")
message(STATUS "  C Standard: ${CMAKE_C_STANDARD}")
message(STATUS "  Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "")
