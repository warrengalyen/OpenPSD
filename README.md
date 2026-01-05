# OpenPSD - Pure C Photoshop File Parser Library

A cross-platform, C library for reading Photoshop PSD and PSB (Large Document Format) files.
Fully compliant with the current Adobe Photoshop File Format Specification. This library is designed to be easy to use and understand, with a focus on performance and portability.

I begin writing this library because all of the low-level libraries I found were limited in their format coverage, failed to read certain files with uncommon structures, had sparse documentation or were no longer maintained. This code originally started as a PSD plugin for a full-featured image editor I'm working on. I decided to extract it into a standalone library for others to reuse in their projects.

I am open to contributions and feedback. Please feel free to open an issue or submit a pull request.
It currently has full support for reading layers like pixel and text layers, which was my original use case. I will be adding support for additional layers types and other features in the future.

## Features

### Supported

- Full parsing of **PSD and PSB** (Large Document Format) files
- Composite (flattened) image parsing and decoding
- Layer parsing:
  - Pixel layer channel data (lazy decode)
  - Layer names (Unicode), bounds, opacity/flags, blend mode
  - Layer groups (folders)
  - Layer type/features detection (group/text/smart object/adjustment/fill/effects/3D/video/empty)
  - Background layer detection (`psd_document_is_background_layer`)
- Text layers (phase-1 API):
  - On-demand extraction of text content and style (font/size/color/tracking/leading/justification)
  - Transform matrix and bounds
- Compression support:
  - RAW
  - RLE
  - ZIP
  - ZIP with prediction
- Color-mode aware rendering APIs:
  - Composite → RGBA8 (`psd_document_render_composite_rgba8[_ex]`)
  - Pixel layer → RGBA8 (`psd_document_render_layer_rgba8`)
- Color modes supported for RGBA8 conversion: RGB, Grayscale, Indexed, CMYK, Lab, Bitmap (plus basic handling for others where possible)

### Limited Support

- Text layer information
  - Advanced features (warp/stroke/multi-run/paragraph composer) are ignored (single-run extraction only)

### Not Supported Yet

- Full layer compositing stack (blending between layers, adjustment application, etc.)
- Layer masks (raster/vector) beyond feature detection
- Shape layers (vector paths)
- Smart Object content extraction/rendering (feature detection only)

## Building

### Requirements

- CMake 3.15 or later
- zlib required for ZIP support
- GTK3 and Cairo required for the demo application

### Build Steps

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

### Build Options

```bash
# Build shared library instead of static
cmake -DBUILD_SHARED_LIBS=ON ..

# Disable tests
cmake -DBUILD_TESTS=OFF ..

# Specify build type
cmake -DCMAKE_BUILD_TYPE=Release ..
```

### Installation

```bash
cmake --install .
```

## Usage

See [API_USAGE](API_USAGE.md) for simple usage examples and a list of public API functions.

See [demo](demo/psd_viewer.c) for a simple example of how to use the library.

## Tests

OpenPSD uses a single unified test runner (`openpsd_tests`) that covers basic parsing, background-layer checks, text layer APIs, color-mode rendering, and sample PSD/PSB parsing.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Note: Sample files are not included in the repository due to their size, so you will need to download them yourself from Git LFS.

```bash
git lfs install
git lfs pull
```

## Demo app

The repository includes a GTK/Cairo demo viewer:

- Source: `demo/psd_viewer.c`
- Built target: `psd_viewer` (only when GTK3 + Cairo are available)

```bash
cmake -S . -B build
cmake --build build --target psd_viewer
```

## Platform-Specific Notes

### Windows

The library handles symbol export/import using `__declspec(dllexport)` and `__declspec(dllimport)` when building shared libraries.

### Linux/macOS

Symbol visibility is controlled using GCC/Clang visibility attributes. Build with `-fvisibility=hidden` to enable proper symbol isolation (this is the default in CMake configuration).

## License

MIT (see [LICENSE](LICENSE))

## References

- [Adobe Photoshop File Format Specification](https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/#50577409_72092)
