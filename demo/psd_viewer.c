#include <cairo.h>
#include <gtk/gtk.h>
#include <openpsd/psd.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *main_box;
    GtkWidget *status_box;
    GtkWidget *status_filetype;
    GtkWidget *status_dimensions;
    GtkWidget *status_mode;
    GtkWidget *status_depth;
    GtkWidget *status_channels;
    GtkWidget *status_layers;
    GtkWidget *canvas;
    GtkWidget *layer_tree;
    GtkWidget *properties_label;
    GtkWidget *always_composite_check;  /* Checkbox to always show composite */
    GtkWidget *fetch_text_metadata_check; /* Checkbox to fetch full text metadata */
    psd_document_t *current_doc;
    psd_stream_t *current_stream;
    uint8_t *file_buffer;
    size_t file_size;
    cairo_surface_t *composite_surface;
    int32_t selected_layer_index; /* Currently selected layer for rendering */
    gboolean show_composite;      /* Show composite or selected layer */
    gboolean always_show_composite; /* Always show composite if available */
    gboolean fetch_text_metadata;  /* Fetch full text layer metadata on selection */
} AppData;

static const char *color_mode_name(psd_color_mode_t m) {
    switch (m) {
    case PSD_COLOR_BITMAP: return "Bitmap";
    case PSD_COLOR_GRAYSCALE: return "Grayscale";
    case PSD_COLOR_INDEXED: return "Indexed";
    case PSD_COLOR_RGB: return "RGB";
    case PSD_COLOR_CMYK: return "CMYK";
    case PSD_COLOR_MULTICHANNEL: return "Multichannel";
    case PSD_COLOR_DUOTONE: return "Duotone";
    case PSD_COLOR_LAB: return "Lab";
    default: return "Unknown";
    }
}

static void update_statusbar(AppData *app)
{
    if (!app) return;

    if (!app->current_doc) {
        if (app->status_filetype) gtk_label_set_text(GTK_LABEL(app->status_filetype), "File: -");
        if (app->status_dimensions) gtk_label_set_text(GTK_LABEL(app->status_dimensions), "Size: -");
        if (app->status_mode) gtk_label_set_text(GTK_LABEL(app->status_mode), "Mode: -");
        if (app->status_depth) gtk_label_set_text(GTK_LABEL(app->status_depth), "Depth: -");
        if (app->status_channels) gtk_label_set_text(GTK_LABEL(app->status_channels), "Channels: -");
        if (app->status_layers) gtk_label_set_text(GTK_LABEL(app->status_layers), "Layers: -");
        return;
    }

    uint32_t w = 0, h = 0;
    psd_color_mode_t mode = (psd_color_mode_t)0;
    uint16_t depth = 0;
    uint16_t channels = 0;
    int32_t layer_count = 0;
    bool is_psb = false;

    (void)psd_document_get_dimensions(app->current_doc, &w, &h);
    (void)psd_document_get_color_mode(app->current_doc, &mode);
    (void)psd_document_get_depth(app->current_doc, &depth);
    (void)psd_document_get_channels(app->current_doc, &channels);
    (void)psd_document_get_layer_count(app->current_doc, &layer_count);
    (void)psd_document_is_psb(app->current_doc, &is_psb);

    char buf[128];

    if (app->status_filetype) {
        snprintf(buf, sizeof(buf), "File: %s", is_psb ? "PSB" : "PSD");
        gtk_label_set_text(GTK_LABEL(app->status_filetype), buf);
    }
    if (app->status_dimensions) {
        snprintf(buf, sizeof(buf), "Size: %ux%u", (unsigned)w, (unsigned)h);
        gtk_label_set_text(GTK_LABEL(app->status_dimensions), buf);
    }
    if (app->status_mode) {
        snprintf(buf, sizeof(buf), "Mode: %s", color_mode_name(mode));
        gtk_label_set_text(GTK_LABEL(app->status_mode), buf);
    }
    if (app->status_depth) {
        snprintf(buf, sizeof(buf), "Depth: %u", (unsigned)depth);
        gtk_label_set_text(GTK_LABEL(app->status_depth), buf);
    }
    if (app->status_channels) {
        snprintf(buf, sizeof(buf), "Channels: %u", (unsigned)channels);
        gtk_label_set_text(GTK_LABEL(app->status_channels), buf);
    }
    if (app->status_layers) {
        snprintf(buf, sizeof(buf), "Layers: %d", (int)layer_count);
        gtk_label_set_text(GTK_LABEL(app->status_layers), buf);
    }
}

static inline uint32_t bytes_per_sample(uint16_t depth_bits) {
    if (depth_bits == 1)
        return 0; /* packed bits */
    if (depth_bits == 8)
        return 1;
    if (depth_bits == 16)
        return 2;
    if (depth_bits == 32)
        return 4;
    return 1;
}

/* Convert a PSD sample to 8-bit for display. */
static inline uint8_t sample_to_u8(const uint8_t *p, uint16_t depth_bits) {
    if (depth_bits == 8)
        return p[0];
    if (depth_bits == 16)
        return p[0]; /* 16-bit BE: take MSB */
    if (depth_bits == 32)
        return p[0]; /* TODO: float conversion */
    return p[0];
}

/* Render a pixel layer into a Cairo ARGB32 surface (premultiplied). */
static cairo_surface_t *render_pixel_layer_surface(psd_document_t *doc, int32_t layer_index) {
    if (!doc)
        return NULL;

    int32_t top, left, bottom, right;
    if (psd_document_get_layer_bounds(doc, layer_index, &top, &left, &bottom, &right) != PSD_OK) {
        return NULL;
    }
    uint32_t width = (right > left) ? (uint32_t)(right - left) : 0;
    uint32_t height = (bottom > top) ? (uint32_t)(bottom - top) : 0;
    if (width == 0 || height == 0) {
        return NULL;
    }

    /* Use library conversion so non-RGB documents (Lab/CMYK/Indexed/etc) render correctly. */
    size_t required = 0;
    psd_status_t st = psd_document_render_layer_rgba8(doc, layer_index, NULL, 0, &required);
    if (st != PSD_OK && st != PSD_ERR_BUFFER_TOO_SMALL) {
        return NULL;
    }
    if (required == 0) {
        return NULL;
    }

    uint8_t *rgba = (uint8_t *)malloc(required);
    if (!rgba) {
        return NULL;
    }

    st = psd_document_render_layer_rgba8(doc, layer_index, rgba, required, NULL);
    if (st != PSD_OK) {
        free(rgba);
        return NULL;
    }

    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (!surf) {
        free(rgba);
        return NULL;
    }
    uint8_t *dst = cairo_image_surface_get_data(surf);
    if (!dst) {
        cairo_surface_destroy(surf);
        free(rgba);
        return NULL;
    }
    int stride = cairo_image_surface_get_stride(surf);

    for (uint32_t y = 0; y < height; y++) {
        uint32_t *row = (uint32_t *)(dst + (int)y * stride);
        for (uint32_t x = 0; x < width; x++) {
            size_t off = ((size_t)y * (size_t)width + (size_t)x) * 4u;
            uint8_t r = rgba[off + 0];
            uint8_t g = rgba[off + 1];
            uint8_t b = rgba[off + 2];
            uint8_t a = rgba[off + 3];
            if (a < 255) {
                r = (uint8_t)((uint16_t)r * (uint16_t)a / 255u);
                g = (uint8_t)((uint16_t)g * (uint16_t)a / 255u);
                b = (uint8_t)((uint16_t)b * (uint16_t)a / 255u);
            }
            row[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    cairo_surface_mark_dirty(surf);
    free(rgba);
    return surf;
}

static gboolean on_canvas_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    AppData *app = (AppData *)user_data;

    /* No document loaded */
    if (!app->current_doc) {
        cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
        cairo_paint(cr);

        cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
        cairo_set_font_size(cr, 14);
        cairo_move_to(cr, 50, 50);
        cairo_show_text(cr, "No PSD file loaded");
        return TRUE;
    }

    /* Check if user wants to always show composite */
    gboolean always_composite = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->always_composite_check));
    
    /* If showing a specific layer and it's renderable (unless always_composite is enabled) */
    if (!always_composite && !app->show_composite && app->selected_layer_index >= 0) {
        psd_layer_type_t layer_type;
        psd_document_get_layer_type(app->current_doc, app->selected_layer_index, &layer_type);
        size_t channel_count;
        psd_document_get_layer_channel_count(app->current_doc, app->selected_layer_index, &channel_count);

        /* Check if it's a pixel layer with channels or a text layer with pixels */
        int can_render = 0;
        if (layer_type == PSD_LAYER_TYPE_PIXEL && channel_count > 0) {
            can_render = 1;
        } else if (layer_type == PSD_LAYER_TYPE_TEXT && channel_count > 0) {
            /* Text layer with rendered pixels */
            can_render = 1;
        }

        if (can_render) {
            int32_t top, left, bottom, right;
            psd_document_get_layer_bounds(app->current_doc, app->selected_layer_index, &top, &left, &bottom, &right);

            /* Draw checkerboard background if the layer has an alpha channel (transparency) */
            gboolean draw_checkerboard = FALSE;
            if (channel_count > 0) {
                for (size_t ch = 0; ch < channel_count; ch++) {
                    int16_t channel_id = 0;
                    const uint8_t *data = NULL;
                    uint64_t len = 0;
                    uint32_t comp = 0;
                    if (psd_document_get_layer_channel_data(app->current_doc, app->selected_layer_index, ch,
                                                           &channel_id, &data, &len, &comp) == PSD_OK) {
                        if (channel_id == -1) { /* alpha */
                            draw_checkerboard = TRUE;
                            break;
                        }
                    }
                }
            }
            
            if (draw_checkerboard) {
                int checker_size = 8;
                
                for (int32_t y = top; y < bottom; y += checker_size) {
                    for (int32_t x = left; x < right; x += checker_size) {
                        int checker_x = (x - left) / checker_size;
                        int checker_y = (y - top) / checker_size;
                        if ((checker_x + checker_y) % 2 == 0) {
                            cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
                        } else {
                            cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
                        }
                        cairo_rectangle(cr, x, y, checker_size, checker_size);
                        cairo_fill(cr);
                    }
                }
            }

            cairo_surface_t *rendered = render_pixel_layer_surface(app->current_doc, app->selected_layer_index);
            if (rendered) {
                cairo_set_source_surface(cr, rendered, left, top);
                cairo_paint(cr);
                cairo_surface_destroy(rendered);
                return TRUE;
            }
        }
    }

    /* Fall back to composite */
    if (!app->composite_surface) {
        cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
        cairo_paint(cr);

        cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
        cairo_set_font_size(cr, 14);
        cairo_move_to(cr, 50, 50);
        cairo_show_text(cr, "No composite image available");
        return TRUE;
    }

    /* Checkerboard background (helps visualize alpha in composite). */
    {
        uint32_t w = 0, h = 0;
        psd_document_get_dimensions(app->current_doc, &w, &h);
        int checker_size = 8;
        for (uint32_t y = 0; y < h; y += (uint32_t)checker_size) {
            for (uint32_t x = 0; x < w; x += (uint32_t)checker_size) {
                if ((((x / (uint32_t)checker_size) + (y / (uint32_t)checker_size)) & 1u) == 0u) {
                    cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
                } else {
                    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
                }
                cairo_rectangle(cr, (double)x, (double)y, checker_size, checker_size);
                cairo_fill(cr);
            }
        }
    }

    cairo_set_source_surface(cr, app->composite_surface, 0, 0);
    cairo_paint(cr);
    return TRUE;
}

/* Get human-readable layer type name from layer type enum */
static const char *get_layer_type_name(psd_layer_type_t type) {
    switch (type) {
    case PSD_LAYER_TYPE_GROUP_END:
        return "Group End";
    case PSD_LAYER_TYPE_GROUP_START:
        return "Group";
    case PSD_LAYER_TYPE_PIXEL:
        return "Pixel";
    case PSD_LAYER_TYPE_TEXT:
        return "Text";
    case PSD_LAYER_TYPE_SMART_OBJECT:
        return "Smart Object";
    case PSD_LAYER_TYPE_ADJUSTMENT:
        return "Adjustment";
    case PSD_LAYER_TYPE_FILL:
        return "Fill";
    case PSD_LAYER_TYPE_EFFECTS:
        return "Effects";
    case PSD_LAYER_TYPE_3D:
        return "3D";
    case PSD_LAYER_TYPE_VIDEO:
        return "Video";
    case PSD_LAYER_TYPE_EMPTY:
        return "Empty";
    default: {
        static char unknown_buf[32];
        snprintf(unknown_buf, sizeof(unknown_buf), "Unknown (%d)", (int)type);
        return unknown_buf;
    }
    }
}

/* Check if layer is supported for rendering based on layer type */
static const char *is_layer_supported(psd_document_t *doc, int32_t layer_index, psd_layer_type_t layer_type) {

    if (layer_type == PSD_LAYER_TYPE_PIXEL || layer_type == PSD_LAYER_TYPE_TEXT) {
        size_t channel_count;
        if (psd_document_get_layer_channel_count(doc, layer_index, &channel_count) == PSD_OK && channel_count > 0) {
            return " ✓"; 
        } else {
            return " ✗"; /* Pixel layer but no pixel data (empty/deleted layer) */
        }
    }
    return " ✗"; /* adjustments, effects, smart objects, etc. are not yet supported */
}

static void thumb_fill_checkerboard(guchar *pixels, int width, int height, int stride, int bpp) {
    const int cell = 6;
    for (int y = 0; y < height; y++) {
        guchar *row = pixels + y * stride;
        for (int x = 0; x < width; x++) {
            const gboolean light = (((x / cell) + (y / cell)) & 1) == 0;
            const guchar bg = light ? 210 : 150;
            row[x * bpp + 0] = bg;
            row[x * bpp + 1] = bg;
            row[x * bpp + 2] = bg;
            if (bpp == 4)
                row[x * bpp + 3] = 255;
        }
    }
}

/* Test whether a given pixel location contains visible content.
* Prefer alpha (if present). Otherwise treat any non-zero RGB as content. */
static inline gboolean is_non_empty_pixel(
    const uint8_t *r_data, const uint8_t *g_data, const uint8_t *b_data, const uint8_t *a_data,
    int width, int x, int y, uint32_t bps, uint16_t depth_bits) 
{
    uint64_t idx = (uint64_t)y * (uint64_t)width + (uint64_t)x;
    const uint8_t *rp = r_data + idx * bps;
    const uint8_t *gp = g_data + idx * bps;
    const uint8_t *bp = b_data + idx * bps;
    if (a_data) {
        const uint8_t *ap = a_data + idx * bps;
        return sample_to_u8(ap, depth_bits) != 0;
    }

    {
        guchar r = sample_to_u8(rp, depth_bits);
        guchar g = sample_to_u8(gp, depth_bits);
        guchar b = sample_to_u8(bp, depth_bits);
        return (r | g | b) != 0;
    }
}

//* Generate a thumbnail pixbuf for a layer (cropped to content bounds). */
static GdkPixbuf *create_layer_thumbnail(psd_document_t *doc, int32_t layer_index, int thumb_size) {
    if (!doc) {
        g_warning("create_layer_thumbnail: NULL document");
        return NULL;
    }

    psd_layer_type_t layer_type;
    if (psd_document_get_layer_type(doc, layer_index, &layer_type) != PSD_OK) {
        g_warning("create_layer_thumbnail: Failed to get layer type for layer %d", layer_index);
        return NULL;
    }

    /* Only generate thumbnails for pixel layers */
    if (layer_type != PSD_LAYER_TYPE_PIXEL) {
        return NULL;
    }

    int32_t top, left, bottom, right;
    if (psd_document_get_layer_bounds(doc, layer_index, &top, &left, &bottom, &right) != PSD_OK) {
        g_warning("create_layer_thumbnail: Failed to get layer bounds for layer %d", layer_index);
        return NULL;
    }

    int32_t width = right - left;
    int32_t height = bottom - top;
    if (width <= 0 || height <= 0) {
        g_warning("create_layer_thumbnail: Invalid layer dimensions for layer %d: %dx%d", layer_index, width, height);
        return NULL;
    }

    size_t channel_count = 0;
    if (psd_document_get_layer_channel_count(doc, layer_index, &channel_count) != PSD_OK) {
        g_warning("create_layer_thumbnail: Failed to get channel count for layer %d", layer_index);
        return NULL;
    }
    if (channel_count == 0) {
        /* Empty/deleted raster layer is possible */
        return NULL;
    }

    uint16_t depth_bits = 8;
    if (psd_document_get_depth(doc, &depth_bits) != PSD_OK) {
        depth_bits = 8;
    }

    const uint32_t bps = bytes_per_sample(depth_bits);

    /* Expected decoded bytes for a single plane */
    uint64_t expected_size = 0;
    if (depth_bits == 1) {
        expected_size = ((uint64_t)width + 7u) / 8u;
        expected_size *= (uint64_t)height;
    } else {
        expected_size = (uint64_t)width * (uint64_t)height * (uint64_t)bps;
    }

    /* Collect planar pointers by channel_id */
    const uint8_t *r_data = NULL, *g_data = NULL, *b_data = NULL, *a_data = NULL;
    uint64_t r_len = 0, g_len = 0, b_len = 0, a_len = 0;

    for (size_t ch = 0; ch < channel_count; ch++) {
        int16_t channel_id = 0;
        const uint8_t *channel_data = NULL;
        uint64_t channel_length = 0;
        uint32_t compression = 0;

        psd_status_t st = psd_document_get_layer_channel_data(
            doc, layer_index, ch, &channel_id,
            &channel_data, &channel_length, &compression);

        if (st != PSD_OK || !channel_data || channel_length == 0) {
            continue;
        }

        /* Require enough decoded bytes for display (RAW may include padding) */
        if (channel_length < expected_size) {
            continue;
        }

        switch (channel_id) {
        case 0:
            r_data = channel_data;
            r_len = channel_length;
            break;
        case 1:
            g_data = channel_data;
            g_len = channel_length;
            break;
        case 2:
            b_data = channel_data;
            b_len = channel_length;
            break;
        case -1:
            a_data = channel_data;
            a_len = channel_length;
            break;
        default:
            break; /* ignore masks/spot/etc */
        }
    }

    if (!r_data || !g_data || !b_data) {
        return NULL;
    }

    if (r_len < expected_size || g_len < expected_size || b_len < expected_size) {
        return NULL;
    }
    if (a_data && a_len < expected_size) {
        a_data = NULL;
    }

    /* Compute thumbnail size that fits the *layer bounds* first (we'll then crop content) */
    double scale_x = (double)thumb_size / (double)width;
    double scale_y = (double)thumb_size / (double)height;
    double scale = (scale_x < scale_y) ? scale_x : scale_y;
    int thumb_width = (int)(width * scale);
    int thumb_height = (int)(height * scale);
    if (thumb_width < 1)
        thumb_width = 1;
    if (thumb_height < 1)
        thumb_height = 1;

    GdkPixbuf *thumbnail = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, thumb_width, thumb_height);
    if (!thumbnail)
        return NULL;

    int thumb_stride = gdk_pixbuf_get_rowstride(thumbnail);
    guchar *thumb_pixels = gdk_pixbuf_get_pixels(thumbnail);
    const int out_bpp = 4;

    /* Background so transparent content doesn't look blank */
    thumb_fill_checkerboard(thumb_pixels, thumb_width, thumb_height, thumb_stride, out_bpp);

    /* Packed 1-bit layers not handled in thumbnails yet */
    if (depth_bits == 1) {
        return thumbnail;
    }

    /* Two-pass bounds: coarse then refine */
    int min_x = INT_MAX, min_y = INT_MAX;
    int max_x = -1, max_y = -1;

    const int coarse_step = 4;
    const int refine_step = 1;

    /* Coarse scan */
    for (int y = 0; y < height; y += coarse_step) {
        for (int x = 0; x < width; x += coarse_step) {
            if (is_non_empty_pixel(r_data, g_data, b_data, a_data, width, x, y, bps, depth_bits)) {
                if (x < min_x)
                    min_x = x;
                if (y < min_y)
                    min_y = y;
                if (x > max_x)
                    max_x = x;
                if (y > max_y)
                    max_y = y;
            }
        }
    }

    /* If no content, keep checkerboard-only thumbnail */
    if (max_x < min_x || max_y < min_y) {
        return thumbnail;
    }

    /* Expand coarse bounds for refinement */
    int rmin_x = min_x - coarse_step;
    if (rmin_x < 0)
        rmin_x = 0;
    int rmin_y = min_y - coarse_step;
    if (rmin_y < 0)
        rmin_y = 0;
    int rmax_x = max_x + coarse_step;
    if (rmax_x > width - 1)
        rmax_x = width - 1;
    int rmax_y = max_y + coarse_step;
    if (rmax_y > height - 1)
        rmax_y = height - 1;

    /* Refine scan */
    min_x = INT_MAX;
    min_y = INT_MAX;
    max_x = -1;
    max_y = -1;

    for (int y = rmin_y; y <= rmax_y; y += refine_step) {
        for (int x = rmin_x; x <= rmax_x; x += refine_step) {
            if (is_non_empty_pixel(r_data, g_data, b_data, a_data, width, x, y, bps, depth_bits)) {
                if (x < min_x)
                    min_x = x;
                if (y < min_y)
                    min_y = y;
                if (x > max_x)
                    max_x = x;
                if (y > max_y)
                    max_y = y;
            }
        }
    }

    if (max_x < min_x || max_y < min_y) {
        return thumbnail;
    }

    /* Expand 1px for visibility */
    if (min_x > 0)
        min_x--;
    if (min_y > 0)
        min_y--;
    if (max_x < width - 1)
        max_x++;
    if (max_y < height - 1)
        max_y++;

    const int crop_w = (max_x - min_x + 1);
    const int crop_h = (max_y - min_y + 1);

    /* Scale cropped content to fit thumbnail */
    double cscale_x = (double)thumb_width / (double)crop_w;
    double cscale_y = (double)thumb_height / (double)crop_h;
    double cscale = (cscale_x < cscale_y) ? cscale_x : cscale_y;
    int draw_w = (int)(crop_w * cscale);
    int draw_h = (int)(crop_h * cscale);
    if (draw_w < 1)
        draw_w = 1;
    if (draw_h < 1)
        draw_h = 1;

    int x_off = (thumb_width - draw_w) / 2;
    int y_off = (thumb_height - draw_h) / 2;

    /* Draw (nearest) and composite over checkerboard */
    for (int y = 0; y < draw_h; y++) {
        int dst_y = y + y_off;
        if (dst_y < 0 || dst_y >= thumb_height)
            continue;
        guchar *row = thumb_pixels + dst_y * thumb_stride;

        int src_y = min_y + (int)((double)y / cscale);
        if (src_y < 0)
            src_y = 0;
        if (src_y >= height)
            src_y = height - 1;

        for (int x = 0; x < draw_w; x++) {
            int dst_x = x + x_off;
            if (dst_x < 0 || dst_x >= thumb_width)
                continue;

            int src_x = min_x + (int)((double)x / cscale);
            if (src_x < 0)
                src_x = 0;
            if (src_x >= width)
                src_x = width - 1;

            uint64_t idx = (uint64_t)src_y * (uint64_t)width + (uint64_t)src_x;
            const uint8_t *rp = r_data + idx * bps;
            const uint8_t *gp = g_data + idx * bps;
            const uint8_t *bp = b_data + idx * bps;
            const uint8_t *ap = a_data ? (a_data + idx * bps) : NULL;

            guchar sr = sample_to_u8(rp, depth_bits);
            guchar sg = sample_to_u8(gp, depth_bits);
            guchar sb = sample_to_u8(bp, depth_bits);
            guchar sa = ap ? sample_to_u8(ap, depth_bits) : 255;

            guchar *dstp = row + dst_x * out_bpp;
            guchar br = dstp[0], bg = dstp[1], bb = dstp[2];

            if (sa == 255) {
                dstp[0] = sr;
                dstp[1] = sg;
                dstp[2] = sb;
            } else if (sa != 0) {
                dstp[0] = (guchar)(((int)sr * (int)sa + (int)br * (255 - (int)sa)) / 255);
                dstp[1] = (guchar)(((int)sg * (int)sa + (int)bg * (255 - (int)sa)) / 255);
                dstp[2] = (guchar)(((int)sb * (int)sa + (int)bb * (255 - (int)sa)) / 255);
            }

            dstp[3] = 255;
        }
    }

    return thumbnail;
}

static void update_layer_list(AppData *app) {
    GtkTreeStore *store = GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(app->layer_tree)));
    gtk_tree_store_clear(store);

    if (!app->current_doc)
        return;

    int32_t layer_count = 0;
    psd_document_get_layer_count(app->current_doc, &layer_count);

    /* Stack to track parent iterators for nested groups */
    GtkTreeIter parent_stack[64];
    int stack_depth = 0;

    /* PSD layers are stored bottom → top; UI wants top → bottom */
    for (int32_t i = layer_count - 1; i >= 0; i--) {
        uint8_t opacity, flags;
        int32_t top, left, bottom, right;
        int32_t width, height;
        size_t channel_count;
        uint32_t blend_sig, blend_key;

        psd_document_get_layer_properties(app->current_doc, i, &opacity, &flags);
        psd_document_get_layer_bounds(app->current_doc, i, &top, &left, &bottom, &right);
        psd_document_get_layer_channel_count(app->current_doc, i, &channel_count);
        psd_document_get_layer_blend_mode(app->current_doc, i, &blend_sig, &blend_key);

        width = right - left;
        height = bottom - top;

        /* Get layer type */
        psd_layer_type_t layer_type;
        psd_document_get_layer_type(app->current_doc, i, &layer_type);

        /* Group end (structural only, never displayed) */
        if (layer_type == PSD_LAYER_TYPE_GROUP_END) {
            if (stack_depth > 0)
                stack_depth--;
            continue;
        }

        /* Get the actual layer name */
        const uint8_t *name_data = NULL;
        size_t name_length = 0;
        psd_document_get_layer_name(app->current_doc, i, &name_data, &name_length);

        /* Convert name to C string (UTF-8) */
        char display_name[512];
        if (name_data && name_length > 0) {
            /* Safely copy name, ensuring null-termination */
            size_t copy_len = name_length < sizeof(display_name) - 1 ? name_length : sizeof(display_name) - 1;
            memcpy(display_name, name_data, copy_len);
            display_name[copy_len] = '\0';

            if (!g_utf8_validate(display_name, (gssize)copy_len, NULL)) {
                snprintf(display_name, sizeof(display_name), "(Invalid UTF-8 name)");
            }
        } else {
            strcpy(display_name, "(Unnamed)");
        }

        /* Determine display label based on layer type */
        const char *type_label = get_layer_type_name(layer_type);
        const char *support_icon = is_layer_supported(app->current_doc, i, layer_type);

        char layer_display[768];
        snprintf(layer_display, sizeof(layer_display),
                 "%s (%s)",
                 display_name, support_icon);

        /* Generate thumbnail for supported layers */
        GdkPixbuf *thumbnail = create_layer_thumbnail(app->current_doc, i, 48);

        /* Insert row */
        GtkTreeIter iter;
        GtkTreeIter *parent = (stack_depth > 0) ? &parent_stack[stack_depth - 1] : NULL;
        gtk_tree_store_append(store, &iter, parent);
        gtk_tree_store_set(store, &iter,
                           0, thumbnail,          // Thumbnail (pixbuf) - NULL if generation failed, GTK takes ownership
                           1, layer_display,      // Layer name
                           2, i,                  // Index
                           3, opacity,            // Opacity
                           4, (int)channel_count, // Channel count
                           5, width,              // Width
                           6, height,             // Height
                           -1);

        /* Note: Don't unref thumbnail - GTK tree store takes ownership (or NULL is fine) */

        /* Group start */
        if (layer_type == PSD_LAYER_TYPE_GROUP_START) {
            if (stack_depth < 63) {
                parent_stack[stack_depth++] = iter;
            }
        }
    }
}

static void update_properties(AppData *app, int32_t layer_index) {
    if (!app->current_doc)
        return;

    psd_color_mode_t doc_mode = (psd_color_mode_t)0;
    psd_document_get_color_mode(app->current_doc, &doc_mode);

    uint8_t opacity, flags;
    int32_t top, left, bottom, right;
    size_t channel_count;
    uint32_t blend_sig, blend_key;

    psd_document_get_layer_properties(app->current_doc, layer_index, &opacity, &flags);
    psd_document_get_layer_bounds(app->current_doc, layer_index, &top, &left, &bottom, &right);
    psd_document_get_layer_channel_count(app->current_doc, layer_index, &channel_count);
    psd_document_get_layer_blend_mode(app->current_doc, layer_index, &blend_sig, &blend_key);

    int32_t width = right - left;
    int32_t height = bottom - top;

    psd_layer_type_t layer_type;
    psd_document_get_layer_type(app->current_doc, layer_index, &layer_type);
    const char *type_name = get_layer_type_name(layer_type);

    /* Check if layer is renderable: must be pixel or text layer with pixel channels */
    size_t layer_channel_count;
    psd_document_get_layer_channel_count(app->current_doc, layer_index, &layer_channel_count);
    const char *support_status = ((layer_type == PSD_LAYER_TYPE_PIXEL || layer_type == PSD_LAYER_TYPE_TEXT) && layer_channel_count > 0) ? "Supported (renderable)" : "Not supported";

    /* Blend mode name */
    const char *blend_name = "Unknown";
    switch (blend_key) {
    case 0x70617373:
        blend_name = "Pass Through";
        break; // 'pass'
    case 0x6e6f726d:
        blend_name = "Normal";
        break; // 'norm'
    case 0x6d756c20:
        blend_name = "Multiply";
        break; // 'mul '
    case 0x73637265:
        blend_name = "Screen";
        break; // 'scrn'
    case 0x6f766572:
        blend_name = "Overlay";
        break; // 'over'
    default:
        break;
    }

    char text[1024];
    int offset = snprintf(text, sizeof(text),
                          "Layer %d Properties:\n\n"
                          "Doc Color Mode: %s (%d)\n"
                          "Type: %s\n"
                          "Support: %s\n"
                          "Bounds: (%d, %d) - (%d, %d)\n"
                          "Size: %d x %d\n"
                          "Opacity: %d/255\n"
                          "Visible: %s\n"
                          "Channels: %zu\n"
                          "Blend Mode: %s",
                          layer_index,
                          color_mode_name(doc_mode), (int)doc_mode,
                          type_name,
                          support_status,
                          left, top, right, bottom,
                          width, height,
                          opacity,
                          (flags & 2) ? "YES" : "NO",
                          channel_count,
                          blend_name);

    /* Add text layer metadata if present */
    if (layer_type == PSD_LAYER_TYPE_TEXT) {
        gboolean fetch_metadata = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->fetch_text_metadata_check));

        psd_text_matrix_t m;
        psd_text_bounds_t b;
        psd_status_t st_mb = psd_text_layer_get_matrix_bounds(
            (psd_document_t *)app->current_doc, (uint32_t)layer_index, &m, &b);

        if (fetch_metadata) {
            char text_content[512] = "";
            psd_status_t text_st = psd_text_layer_get_text(
                (psd_document_t *)app->current_doc,
                (uint32_t)layer_index,
                text_content,
                sizeof(text_content));
            if (text_st != PSD_OK) {
                snprintf(text_content, sizeof(text_content),
                         "<failed to extract text: %s (%d)>",
                         psd_error_string(text_st), (int)text_st);
            }

            psd_text_style_t style;
            psd_status_t st_style = psd_text_layer_get_default_style(
                (psd_document_t *)app->current_doc, (uint32_t)layer_index, &style);

            const char *just = "left";
            if (st_style == PSD_OK) {
                switch (style.justification) {
                case PSD_TEXT_JUSTIFY_LEFT: just = "left"; break;
                case PSD_TEXT_JUSTIFY_RIGHT: just = "right"; break;
                case PSD_TEXT_JUSTIFY_CENTER: just = "center"; break;
                case PSD_TEXT_JUSTIFY_FULL: just = "full"; break;
                default: just = "left"; break;
                }
            }

            offset += snprintf(text + offset, sizeof(text) - offset,
                               "\n\nText Layer:\n"
                               "Content: %s\n"
                               "Default Style:\n"
                               "  Font: %s\n"
                               "  Size: %.2f\n"
                               "  Color: rgba(%u,%u,%u,%u)\n"
                               "  Tracking: %.2f\n"
                               "  Leading: %.2f\n"
                               "  Justification: %s\n"
                               "Transform: (%.2f, %.2f) (%.2f, %.2f) (%.2f, %.2f)\n"
                               "Text Bounds: (%.0f, %.0f) - (%.0f, %.0f)",
                               text_content,
                               (st_style == PSD_OK) ? style.font_name : "<unavailable>",
                               (st_style == PSD_OK) ? style.size : 0.0,
                               (unsigned)(st_style == PSD_OK ? style.color_rgba[0] : 0),
                               (unsigned)(st_style == PSD_OK ? style.color_rgba[1] : 0),
                               (unsigned)(st_style == PSD_OK ? style.color_rgba[2] : 0),
                               (unsigned)(st_style == PSD_OK ? style.color_rgba[3] : 255),
                               (st_style == PSD_OK) ? style.tracking : 0.0,
                               (st_style == PSD_OK) ? style.leading : 0.0,
                               just,
                               (st_mb == PSD_OK) ? m.xx : 0.0, (st_mb == PSD_OK) ? m.xy : 0.0,
                               (st_mb == PSD_OK) ? m.yx : 0.0, (st_mb == PSD_OK) ? m.yy : 0.0,
                               (st_mb == PSD_OK) ? m.tx : 0.0, (st_mb == PSD_OK) ? m.ty : 0.0,
                               (st_mb == PSD_OK) ? b.left : 0.0, (st_mb == PSD_OK) ? b.top : 0.0,
                               (st_mb == PSD_OK) ? b.right : 0.0, (st_mb == PSD_OK) ? b.bottom : 0.0);
        } else {
            offset += snprintf(text + offset, sizeof(text) - offset,
                               "\n\nText Layer:\n"
                               "Transform: (%.2f, %.2f) (%.2f, %.2f) (%.2f, %.2f)\n"
                               "Text Bounds: (%.0f, %.0f) - (%.0f, %.0f)",
                               (st_mb == PSD_OK) ? m.xx : 0.0, (st_mb == PSD_OK) ? m.xy : 0.0,
                               (st_mb == PSD_OK) ? m.yx : 0.0, (st_mb == PSD_OK) ? m.yy : 0.0,
                               (st_mb == PSD_OK) ? m.tx : 0.0, (st_mb == PSD_OK) ? m.ty : 0.0,
                               (st_mb == PSD_OK) ? b.left : 0.0, (st_mb == PSD_OK) ? b.top : 0.0,
                               (st_mb == PSD_OK) ? b.right : 0.0, (st_mb == PSD_OK) ? b.bottom : 0.0);
        }
    }

    gtk_label_set_text(GTK_LABEL(app->properties_label), text);
}

static void on_layer_selected(GtkTreeSelection *selection, gpointer user_data) {
    AppData *app = (AppData *)user_data;
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        int32_t layer_index;
        gtk_tree_model_get(model, &iter, 2, &layer_index, -1); // Index is now at column 2
        app->selected_layer_index = layer_index;

        /* Check if this layer is renderable */
        psd_layer_type_t layer_type;
        psd_document_get_layer_type(app->current_doc, layer_index, &layer_type);
        size_t channel_count;
        psd_document_get_layer_channel_count(app->current_doc, layer_index, &channel_count);

        /* Show selected layer if it's a pixel or text layer with data */
        /* Groups and other layer types show the composite image */
        if ((layer_type == PSD_LAYER_TYPE_PIXEL || layer_type == PSD_LAYER_TYPE_TEXT) && channel_count > 0) {
            app->show_composite = FALSE;
        } else {
            /* Groups, adjustment layers, etc. */
            app->show_composite = TRUE;
        }
        gtk_widget_queue_draw(app->canvas);

        update_properties(app, layer_index);
    }
}

static void load_psd_file(AppData *app, const char *filename) {
    // Free previous document
    if (app->current_doc) {
        psd_document_free(app->current_doc);
        app->current_doc = NULL;
    }
    if (app->current_stream) {
        psd_stream_destroy(app->current_stream);
        app->current_stream = NULL;
    }
    if (app->file_buffer) {
        free(app->file_buffer);
        app->file_buffer = NULL;
    }
    if (app->composite_surface) {
        cairo_surface_destroy(app->composite_surface);
        app->composite_surface = NULL;
    }

    // Load file
    FILE *f = fopen(filename, "rb");
    if (!f) {
        g_warning("Could not open file: %s", filename);
        return;
    }

    fseek(f, 0, SEEK_END);
    app->file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    app->file_buffer = malloc(app->file_size);
    if (!app->file_buffer) {
        g_warning("Out of memory");
        fclose(f);
        return;
    }

    fread(app->file_buffer, 1, app->file_size, f);
    fclose(f);

    // Create stream and parse
    app->current_stream = psd_stream_create_buffer(NULL, app->file_buffer, app->file_size);
    if (!app->current_stream) {
        g_warning("Could not create stream");
        return;
    }

    app->current_doc = psd_parse(app->current_stream, NULL);
    if (!app->current_doc) {
        g_warning("Could not parse PSD file");
        psd_stream_destroy(app->current_stream);
        app->current_stream = NULL;
        update_statusbar(app);
        return;
    }

    // Get document dimensions
    uint32_t width, height;
    psd_document_get_dimensions(app->current_doc, &width, &height);

    // Create Cairo surface for composite image
    app->composite_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);

    uint16_t depth_bits = 8;
    psd_document_get_depth(app->current_doc, &depth_bits);

    psd_color_mode_t color_mode = (psd_color_mode_t)0;
    psd_document_get_color_mode(app->current_doc, &color_mode);

    uint16_t channel_count = 0;
    psd_document_get_channels(app->current_doc, &channel_count);

    g_print("Doc: %ux%u depth=%u mode=%d (%s) channels=%u\n",
            width, height,
            (unsigned)depth_bits,
            (int)color_mode, color_mode_name(color_mode),
            (unsigned)channel_count);

    /* Render composite via library color conversion to RGBA8 */
    size_t required = 0;
    psd_status_t status = psd_document_render_composite_rgba8(app->current_doc, NULL, 0, &required);
    if (status == PSD_OK || status == PSD_ERR_BUFFER_TOO_SMALL) {
        uint8_t *rgba = (uint8_t *)malloc(required);
        if (!rgba) {
            status = PSD_ERR_OUT_OF_MEMORY;
        } else {
            status = psd_document_render_composite_rgba8(app->current_doc, rgba, required, NULL);
            if (status == PSD_OK) {
                uint8_t *surface_data = cairo_image_surface_get_data(app->composite_surface);
                int stride = cairo_image_surface_get_stride(app->composite_surface);
                for (uint32_t y = 0; y < height; y++) {
                    uint32_t *row = (uint32_t *)(surface_data + y * stride);
                    for (uint32_t x = 0; x < width; x++) {
                        size_t off = ((size_t)y * (size_t)width + (size_t)x) * 4u;
                        uint8_t r = rgba[off + 0];
                        uint8_t g = rgba[off + 1];
                        uint8_t b = rgba[off + 2];
                        uint8_t a = rgba[off + 3];
                        if (a < 255) {
                            r = (uint8_t)((uint16_t)r * (uint16_t)a / 255u);
                            g = (uint8_t)((uint16_t)g * (uint16_t)a / 255u);
                            b = (uint8_t)((uint16_t)b * (uint16_t)a / 255u);
                        }
                        row[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                    }
                }
                cairo_surface_mark_dirty(app->composite_surface);
            }
            free(rgba);
        }
    }

    if (status != PSD_OK) {
        g_print("No composite image available (status=%d). Drawing gray checkerboard instead.\n", status);

        // Draw a gray checkerboard as placeholder
        uint8_t *surface_data = cairo_image_surface_get_data(app->composite_surface);
        int stride = cairo_image_surface_get_stride(app->composite_surface);

        for (uint32_t y = 0; y < height; y++) {
            uint32_t *row = (uint32_t *)(surface_data + y * stride);
            for (uint32_t x = 0; x < width; x++) {
                uint8_t color = ((x / 32) + (y / 32)) % 2 == 0 ? 200 : 100;
                // BGRA format
                row[x] = 0xFF000000 | (color << 16) | (color << 8) | color;
            }
        }
        cairo_surface_mark_dirty(app->composite_surface);
    }

    // Initialize viewer state
    app->selected_layer_index = -1;
    app->show_composite = TRUE;

    // Update layer list
    update_layer_list(app);

    // Resize canvas to fit image
    gtk_widget_set_size_request(app->canvas, width, height);

    g_print("Loaded PSD canvas: %ux%u\n", width, height);
    update_statusbar(app);
}

static void on_open_file(GtkWidget *widget, gpointer user_data) {
    AppData *app = (AppData *)user_data;
    GtkWidget *dialog;

    dialog = gtk_file_chooser_dialog_new("Open PSD File",
                                         GTK_WINDOW(app->window),
                                         GTK_FILE_CHOOSER_ACTION_OPEN,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Open", GTK_RESPONSE_ACCEPT,
                                         NULL);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Photoshop Files");
    gtk_file_filter_add_pattern(filter, "*.psd");
    gtk_file_filter_add_pattern(filter, "*.psb");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        load_psd_file(app, filename);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

static gboolean on_window_close(GtkWidget *widget, gpointer user_data) {
    AppData *app = (AppData *)user_data;

    if (app->current_doc)
        psd_document_free(app->current_doc);
    if (app->current_stream)
        psd_stream_destroy(app->current_stream);
    if (app->file_buffer)
        free(app->file_buffer);
    if (app->composite_surface)
        cairo_surface_destroy(app->composite_surface);

    gtk_main_quit();
    return FALSE;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    AppData app = {0};

    // Create main window
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "PSD Viewer");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 1200, 800);
    gtk_window_set_position(GTK_WINDOW(app.window), GTK_WIN_POS_CENTER);
    g_signal_connect(app.window, "delete-event", G_CALLBACK(on_window_close), &app);

    // Create root layout (main content + status bar)
    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(root_box), 5);
    gtk_container_add(GTK_CONTAINER(app.window), root_box);

    // Main content layout (left canvas + right layers/properties)
    app.main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(root_box), app.main_box, TRUE, TRUE, 0);

    // Left panel: Canvas for image
    GtkWidget *left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *open_button = gtk_button_new_with_label("Open PSD...");
    g_signal_connect(open_button, "clicked", G_CALLBACK(on_open_file), &app);
    gtk_box_pack_start(GTK_BOX(button_box), open_button, FALSE, FALSE, 0);
    
    /* Add checkboxes */
    app.always_composite_check = gtk_check_button_new_with_label("Always Show Composite");
    gtk_box_pack_start(GTK_BOX(button_box), app.always_composite_check, FALSE, FALSE, 0);
    
    app.fetch_text_metadata_check = gtk_check_button_new_with_label("Fetch Text Metadata");
    gtk_box_pack_start(GTK_BOX(button_box), app.fetch_text_metadata_check, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(left_box), button_box, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    app.canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.canvas, 512, 512);
    g_signal_connect(app.canvas, "draw", G_CALLBACK(on_canvas_draw), &app);
    gtk_container_add(GTK_CONTAINER(scroll), app.canvas);

    gtk_box_pack_start(GTK_BOX(left_box), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(app.main_box), left_box, TRUE, TRUE, 0);

    // Right panel: Layer list and properties
    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_size_request(right_box, 300, -1);

    GtkWidget *layers_label = gtk_label_new("Layers:");
    gtk_box_pack_start(GTK_BOX(right_box), layers_label, FALSE, FALSE, 0);

    GtkTreeStore *layer_store = gtk_tree_store_new(7,
                                                   GDK_TYPE_PIXBUF, // Thumbnail
                                                   G_TYPE_STRING,   // Layer name
                                                   G_TYPE_INT,      // Index
                                                   G_TYPE_INT,      // Opacity
                                                   G_TYPE_INT,      // Channel count
                                                   G_TYPE_INT,      // Width
                                                   G_TYPE_INT);     // Height

    app.layer_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(layer_store));

    /* Enable expanders for hierarchical display */
    gtk_tree_view_set_show_expanders(GTK_TREE_VIEW(app.layer_tree), TRUE);
    gtk_tree_view_set_level_indentation(GTK_TREE_VIEW(app.layer_tree), 20);

    /* Add thumbnail column */
    GtkCellRenderer *pixbuf_renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_renderer_set_padding(pixbuf_renderer, 2, 2);
    GtkTreeViewColumn *thumb_column = gtk_tree_view_column_new_with_attributes("",
                                                                               pixbuf_renderer,
                                                                               "pixbuf", 0,
                                                                               NULL);
    gtk_tree_view_column_set_min_width(thumb_column, 48);
    gtk_tree_view_column_set_sizing(thumb_column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(thumb_column, 52); /* 48 + 4 for padding */
    gtk_tree_view_append_column(GTK_TREE_VIEW(app.layer_tree), thumb_column);

    /* Add layer name column */
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Layer",
                                                                         renderer,
                                                                         "text", 1,
                                                                         NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(app.layer_tree), column);

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(app.layer_tree));
    g_signal_connect(selection, "changed", G_CALLBACK(on_layer_selected), &app);

    GtkWidget *tree_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tree_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(tree_scroll), app.layer_tree);
    gtk_box_pack_start(GTK_BOX(right_box), tree_scroll, TRUE, TRUE, 0);

    GtkWidget *props_label = gtk_label_new("Properties:");
    gtk_box_pack_start(GTK_BOX(right_box), props_label, FALSE, FALSE, 0);

    app.properties_label = gtk_label_new("No layer selected");
    gtk_label_set_line_wrap(GTK_LABEL(app.properties_label), TRUE);
    gtk_box_pack_start(GTK_BOX(right_box), app.properties_label, FALSE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(app.main_box), right_box, FALSE, TRUE, 0);

    // Status bar (document-level info)
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(root_box), sep, FALSE, FALSE, 0);

    app.status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(root_box), app.status_box, FALSE, FALSE, 0);

    app.status_filetype = gtk_label_new("File: -");
    app.status_dimensions = gtk_label_new("Size: -");
    app.status_mode = gtk_label_new("Mode: -");
    app.status_depth = gtk_label_new("Depth: -");
    app.status_channels = gtk_label_new("Channels: -");
    app.status_layers = gtk_label_new("Layers: -");

    gtk_box_pack_start(GTK_BOX(app.status_box), app.status_filetype, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(app.status_box), app.status_dimensions, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(app.status_box), app.status_mode, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(app.status_box), app.status_depth, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(app.status_box), app.status_channels, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(app.status_box), app.status_layers, FALSE, FALSE, 0);

    update_statusbar(&app);

    // Show all
    gtk_widget_show_all(app.window);

    // Main loop
    gtk_main();

    return 0;
}
