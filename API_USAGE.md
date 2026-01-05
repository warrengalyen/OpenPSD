# OpenPSD API Usage

This document lists the **public C API** exposed by OpenPSD with short usage examples.

Headers:
- `#include <openpsd/psd.h>`
- `#include <openpsd/psd_stream.h>`
- `#include <openpsd/psd_error.h>`

---

## Version

### `psd_get_version`

```c
printf("OpenPSD version: %s\n", psd_get_version());
```

### `psd_version_components`

```c
int major = 0, minor = 0, patch = 0;
psd_version_components(&major, &minor, &patch);
printf("OpenPSD version: %d.%d.%d\n", major, minor, patch);
```

---

## Error strings

### `psd_error_string`

```c
psd_status_t st = PSD_ERR_CORRUPT_DATA;
fprintf(stderr, "error: %s (%d)\n", psd_error_string(st), (int)st);
```

---

## Streams (`psd_stream_t`)

### `psd_stream_create_buffer`

```c
psd_stream_t *s = psd_stream_create_buffer(NULL, file_bytes, file_size);
if (!s) { /* OOM */ }
```

### `psd_stream_create_custom`

```c
psd_stream_vtable_t v = {0};
v.read = my_read;
v.write = my_write;
v.seek = my_seek;
v.tell = my_tell;
v.close = my_close; /* optional */

psd_stream_t *s = psd_stream_create_custom(NULL, &v, my_user_data);
```

### `psd_stream_destroy`

```c
psd_stream_destroy(s);
```

### `psd_stream_read`

```c
uint8_t buf[16];
int64_t n = psd_stream_read(s, buf, sizeof(buf));
if (n < 0) { /* error code */ }
```

### `psd_stream_write`

```c
const uint8_t out[] = {1,2,3};
int64_t n = psd_stream_write(s, out, sizeof(out));
if (n < 0) { /* error code */ }
```

### `psd_stream_seek`

```c
int64_t pos = psd_stream_seek(s, 0); /* seek to start */
if (pos < 0) { /* error */ }
```

### `psd_stream_tell`

```c
int64_t pos = psd_stream_tell(s);
```

### `psd_stream_read_exact`

```c
uint8_t hdr[26];
psd_status_t st = psd_stream_read_exact(s, hdr, sizeof(hdr));
if (st != PSD_OK) { /* handle */ }
```

### `psd_stream_read_be16` / `psd_stream_read_be32` / `psd_stream_read_be64` / `psd_stream_read_be_i32`

```c
uint32_t v32 = 0;
psd_stream_read_be32(s, &v32);
```

### `psd_stream_skip`

```c
psd_stream_skip(s, 128);
```

### `psd_stream_read_length`

```c
uint64_t len = 0;
psd_status_t st = psd_stream_read_length(s, /*is_psb=*/false, &len);
```

---

## Parsing / lifetime (`psd_document_t`)

### `psd_parse`

```c
psd_stream_t *s = psd_stream_create_buffer(NULL, file_bytes, file_size);
psd_document_t *doc = psd_parse(s, NULL);
psd_stream_destroy(s); /* stream can be destroyed after parse */
if (!doc) { /* parse failed (no status) */ }
```

### `psd_parse_ex`

```c
psd_status_t st = PSD_OK;
psd_document_t *doc = psd_parse_ex(s, NULL, &st);
if (!doc) {
    fprintf(stderr, "parse failed: %s (%d)\n", psd_error_string(st), (int)st);
}
```

### `psd_document_free`

```c
psd_document_free(doc);
```

---

## Document queries

### `psd_document_get_dimensions`

```c
uint32_t w = 0, h = 0;
psd_document_get_dimensions(doc, &w, &h);
```

### `psd_document_get_color_mode`

```c
psd_color_mode_t mode = (psd_color_mode_t)0;
psd_document_get_color_mode(doc, &mode);
```

### `psd_document_get_depth`

```c
uint16_t depth = 0;
psd_document_get_depth(doc, &depth); /* 1, 8, 16, or 32 */
```

### `psd_document_get_channels`

```c
uint16_t channels = 0;
psd_document_get_channels(doc, &channels);
```

### `psd_document_is_psb`

```c
bool is_psb = false;
psd_document_is_psb(doc, &is_psb);
```

### `psd_document_get_color_mode_data`

```c
const uint8_t *data = NULL;
uint64_t len = 0;
psd_document_get_color_mode_data(doc, &data, &len);
```

---

## Image resources

### `psd_document_get_resource_count`

```c
size_t count = 0;
psd_document_get_resource_count(doc, &count);
```

### `psd_document_get_resource`

```c
uint16_t id = 0;
const uint8_t *data = NULL;
uint64_t len = 0;
psd_document_get_resource(doc, 0, &id, &data, &len);
```

### `psd_document_find_resource`

```c
size_t idx = 0;
psd_status_t st = psd_document_find_resource(doc, /*resource_id=*/1005, &idx);
if (st == PSD_OK) {
    /* found at idx */
}
```

---

## Layers

### `psd_document_get_layer_count`

```c
int32_t n = 0;
psd_document_get_layer_count(doc, &n);
```

### `psd_document_has_transparency_layer`

```c
bool has_tl = false;
psd_document_has_transparency_layer(doc, &has_tl);
```

### `psd_document_get_layer_bounds`

```c
int32_t top, left, bottom, right;
psd_document_get_layer_bounds(doc, layer_index, &top, &left, &bottom, &right);
```

### `psd_document_get_layer_blend_mode`

```c
uint32_t sig = 0, key = 0;
psd_document_get_layer_blend_mode(doc, layer_index, &sig, &key);
/* sig/key are 4-byte codes (e.g. '8BIM' / 'norm') */
```

### `psd_document_get_layer_properties`

```c
uint8_t opacity = 0, flags = 0;
psd_document_get_layer_properties(doc, layer_index, &opacity, &flags);
```

### `psd_document_get_layer_channel_count`

```c
size_t nchan = 0;
psd_document_get_layer_channel_count(doc, layer_index, &nchan);
```

### `psd_document_get_layer_name`

```c
const uint8_t *name = NULL;
size_t name_len = 0;
psd_document_get_layer_name(doc, layer_index, &name, &name_len);
printf("layer name: %.*s\n", (int)name_len, (const char*)name);
```

### `psd_document_get_layer_features`

```c
psd_layer_features_t f = {0};
psd_document_get_layer_features(doc, layer_index, &f);
```

### `psd_document_get_layer_type`

```c
psd_layer_type_t t = PSD_LAYER_TYPE_EMPTY;
psd_document_get_layer_type(doc, layer_index, &t);
```

### `psd_document_is_background_layer`

```c
/* base_channel_count depends on document color mode: RGB=3, CMYK=4, etc. */
int base_channels = 3;
psd_bool_t is_bg = psd_document_is_background_layer(doc, layer_index, base_channels);
```

---

## Composite image access + rendering

### `psd_document_get_composite_image`

```c
const uint8_t *data = NULL;
uint64_t len = 0;
uint32_t compression = 0;
psd_document_get_composite_image(doc, &data, &len, &compression);
/* data is planar channel data in the PSD's native color mode */
```

### `psd_document_render_composite_rgba8`

```c
size_t required = 0;
psd_status_t st = psd_document_render_composite_rgba8(doc, NULL, 0, &required);
if (st == PSD_OK || st == PSD_ERR_BUFFER_TOO_SMALL) {
    uint8_t *rgba = malloc(required);
    st = psd_document_render_composite_rgba8(doc, rgba, required, NULL);
    /* rgba is interleaved RGBA8 (non-premultiplied) */
    free(rgba);
}
```

### `psd_document_render_composite_rgba8_ex`

```c
size_t required = 0;
psd_render_composite_info_t info;
psd_status_t st = psd_document_render_composite_rgba8_ex(doc, NULL, 0, &required, &info);
```

---

## Layer rendering + channel access

### `psd_document_render_layer_rgba8`

```c
size_t required = 0;
psd_status_t st = psd_document_render_layer_rgba8(doc, layer_index, NULL, 0, &required);
if (st == PSD_OK || st == PSD_ERR_BUFFER_TOO_SMALL) {
    uint8_t *rgba = malloc(required);
    st = psd_document_render_layer_rgba8(doc, layer_index, rgba, required, NULL);
    free(rgba);
}
```

### `psd_document_get_layer_channel_data`

```c
int16_t channel_id = 0;
const uint8_t *plane = NULL;
uint64_t len = 0;
uint32_t compression = 0;

psd_status_t st = psd_document_get_layer_channel_data(
    doc, layer_index, channel_index,
    &channel_id, &plane, &len, &compression);
```

### `psd_document_get_layer_descriptor`

```c
const uint8_t *desc = NULL;
uint64_t desc_len = 0;
psd_document_get_layer_descriptor(doc, layer_index, &desc, &desc_len);
/* desc is raw bytes (if present); interpretation is application-specific */
```

---

## Text layer API (phase 1)

### `psd_text_layer_get_text`

```c
char text[1024];
psd_status_t st = psd_text_layer_get_text(doc, layer_index, text, sizeof(text));
if (st == PSD_OK) {
    printf("text: %s\n", text);
}
```

### `psd_text_layer_get_default_style`

```c
psd_text_style_t style;
psd_status_t st = psd_text_layer_get_default_style(doc, layer_index, &style);
```

### `psd_text_layer_get_matrix_bounds`

```c
psd_text_matrix_t m;
psd_text_bounds_t b;
psd_status_t st = psd_text_layer_get_matrix_bounds(doc, layer_index, &m, &b);
```
