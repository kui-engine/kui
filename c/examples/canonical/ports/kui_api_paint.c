/* API tour: host painting into custom elements (examples/api_tours/paint). Before the page loads
 * the host defines six custom elements with a paint callback (define_custom_element_ex); each
 * callback draws one group of kui_paint_* calls into its content box. Once frames have run, the
 * host checks the callbacks ran with the element's size and every call succeeded, asks for
 * repaints (request_repaint / invalidate_rect) and checks they arrive; #tour-result ends as
 * "PASS n/n". What is drawn is checked by the case's pixel steps; the buttons change it. */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS /* getenv / fopen for the image file the host writes */
#endif
#include "kui_c_example_dom.h"

#include <stdarg.h>
#include <stdlib.h>

static KuiDocument* g_doc;

/* ---- Self-check bookkeeping: count, log one <tr> per check, publish the verdict ---------- */

static int g_total;
static int g_failed;
static char g_failed_names[512];

static void log_row(const char* name, const char* result, const char* cell) {
    KuiElement* log = kuex_by_id(g_doc, "tour-log");
    KuiElement* row = NULL;
    if (!log) return;
    if (kui_document_create_element(g_doc, "tr", 0, &row) == KUI_OK) {
        kui_element_set_attribute(row, "data-check", 0, name, 0);
        kui_element_set_attribute(row, "data-result", 0, result, 0);
        if (strcmp(result, "ok") != 0) kui_element_class_list_add(row, "bad", 0);
        kuex_append_release(row, kuex_make_element(g_doc, "td", name, NULL));
        kuex_append_release(row, kuex_make_element(g_doc, "td", cell, NULL));
        kuex_append_release(log, row);
    }
    kui_element_release(log);
}

static void check(const char* name, int ok, const char* fmt, ...) {
    char detail[256];
    char cell[300];
    size_t used = strlen(g_failed_names);
    va_list args;

    g_total++;
    if (ok) {
        log_row(name, "ok", "ok");
        return;
    }
    va_start(args, fmt);
    vsnprintf(detail, sizeof detail, fmt, args);
    va_end(args);
    g_failed++;
    snprintf(g_failed_names + used, sizeof g_failed_names - used, "%s%s", used ? ", " : "", name);
    snprintf(cell, sizeof cell, "FAIL: %s", detail);
    fprintf(stderr, "tour check failed: %s: %s\n", name, detail);
    log_row(name, "fail", cell);
}

static void finish(void) {
    char text[640];
    char number[16];
    KuiElement* result = kuex_by_id(g_doc, "tour-result");
    if (!result) return;
    if (g_failed == 0) {
        snprintf(text, sizeof text, "PASS %d/%d", g_total, g_total);
    } else {
        snprintf(text, sizeof text, "FAIL %d/%d: %s", g_failed, g_total, g_failed_names);
    }
    kui_element_set_text_content(result, text, 0);
    snprintf(number, sizeof number, "%d", g_total);
    kui_element_set_attribute(result, "data-total", 0, number, 0);
    snprintf(number, sizeof number, "%d", g_failed);
    kui_element_set_attribute(result, "data-failed", 0, number, 0);
    kui_element_set_attribute(result, "data-known", 0, "0", 0);
    kui_element_class_list_add(result, g_failed == 0 ? "pass" : "fail", 0);
    kui_element_release(result);
}

/* ---- What the panes draw (the buttons change these) and what the callbacks saw ------------ */

enum { SHAPES, TEXT, IMAGE, CLIP, XFORM, ALPHA, PANES };
static const char* const g_tags[PANES] = {"paint-shapes", "paint-text", "paint-image",
                                          "paint-clip", "paint-xform", "paint-alpha"};
static const char* const g_ids[PANES] = {"pv-shapes", "pv-text", "pv-image",
                                         "pv-clip", "pv-xform", "pv-alpha"};
static int g_pane_index[PANES] = {SHAPES, TEXT, IMAGE, CLIP, XFORM, ALPHA};

static int g_paints[PANES];          /* paint callback calls */
static float g_width[PANES];         /* kui_paint_get_info, last call */
static float g_height[PANES];
static float g_dpr[PANES];
static int g_bad_calls[PANES];       /* kui_paint_* calls that did not return KUI_OK */
static KuiStatus g_unbalanced = KUI_OK; /* restore without save, tried once */
static int g_unbalanced_tried;

static uint32_t g_fill = 0xef4444ffu;     /* shapes: first rectangle (0xRRGGBBAA) */
static int g_line_style = KUI_C_DECORATION_SOLID;
static uint32_t g_line = 0xe11d48ffu;     /* text: decoration line colour */
static int g_pixel_shift;                 /* image: rotates the 2x2 buffer */
static int g_clip_on = 1;
static float g_angle = 45.0f;
static float g_alpha = 0.5f;
static char g_image_path[512];

static const char* const g_style_names[] = {"solid", "double", "dotted", "dashed", "wavy"};

static void ok(int pane, KuiStatus status) {
    if (status != KUI_OK) g_bad_calls[pane]++;
}

static KuiCCornerRadii radii(float r) {
    KuiCCornerRadii value;
    value.top_left_x = value.top_left_y = value.top_right_x = value.top_right_y = r;
    value.bottom_right_x = value.bottom_right_y = value.bottom_left_x = value.bottom_left_y = r;
    return value;
}

/* ---- Paint callbacks: one group of calls per pane ----------------------------------------- */

/* Filled and stroked rectangles at fixed fractions of the width (the case samples them). */
static void paint_shapes(KuiPaintContext* p, float w, float h) {
    KuiCCornerRadii r16 = radii(16.0f);
    KuiCCornerRadii r14 = radii(14.0f);
    ok(SHAPES, kui_paint_fill_rect(p, 0.03f * w, 0.15f * h, 0.20f * w, 0.70f * h, g_fill));
    ok(SHAPES, kui_paint_fill_rounded_rect(p, 0.28f * w, 0.15f * h, 0.20f * w, 0.70f * h, &r16,
                                           0x22c55effu));
    ok(SHAPES, kui_paint_stroke_rect(p, 0.53f * w, 0.15f * h, 0.20f * w, 0.70f * h, 8.0f,
                                     0x3b82f6ffu));
    ok(SHAPES, kui_paint_stroke_rounded_rect(p, 0.78f * w, 0.15f * h, 0.19f * w, 0.70f * h, &r14,
                                             8.0f, 0xa855f7ffu));
}

/* Bold text hanging from y (the top of its line box, not the baseline), the decoration line band at 0.6h..0.6h+8 and a wavy line below. */
static void paint_text(KuiPaintContext* p, float w, float h) {
    KuiCTextStyle style;
    memset(&style, 0, sizeof style);
    style.font_size = 26.0f;
    style.font_weight = 700;
    ok(TEXT, kui_paint_draw_text(p, "Hello paint", 0, 10.0f, 0.45f * h, &style, 0x0f172affu));
    ok(TEXT, kui_paint_draw_decoration_line(p, 10.0f, 0.6f * h, 0.9f * w, 8.0f,
                                            (KuiCDecorationStyle)g_line_style, g_line));
    ok(TEXT, kui_paint_draw_decoration_line(p, 10.0f, 0.82f * h, 0.9f * w, 2.0f,
                                            KUI_C_DECORATION_WAVY, 0x0ea5e9ffu));
}

/* A 2x2 premultiplied 0xAARRGGBB buffer scaled into the left box, an image file into the right. */
static void paint_image(KuiPaintContext* p, float w, float h) {
    static const uint32_t colors[4] = {0xffff0000u, 0xff00ff00u, 0xff0000ffu, 0xffffff00u};
    uint32_t pixels[4];
    KuiRect src;
    KuiRect dest;
    int index;
    for (index = 0; index < 4; ++index) pixels[index] = colors[(index + g_pixel_shift) % 4];
    ok(IMAGE, kui_paint_draw_pixels(p, pixels, 2, 2, 0.03f * w, 0.1f * h, 0.4f * w, 0.8f * h));
    src.x = 0;
    src.y = 0;
    src.width = 8;
    src.height = 8;
    dest.x = 0.55f * w;
    dest.y = 0.1f * h;
    dest.width = 0.4f * w;
    dest.height = 0.8f * h;
    ok(IMAGE, kui_paint_draw_image(p, g_image_path, 0, &src, &dest));
}

/* Top band clipped to the left half; bottom band clipped to a rounded rectangle. */
static void paint_clip(KuiPaintContext* p, float w, float h) {
    KuiCCornerRadii r24 = radii(24.0f);
    ok(CLIP, kui_paint_save(p));
    if (g_clip_on) ok(CLIP, kui_paint_clip_rect(p, 0, 0, 0.5f * w, h));
    ok(CLIP, kui_paint_fill_rect(p, 0, 0, w, 0.45f * h, 0xf97316ffu));
    ok(CLIP, kui_paint_restore(p));
    ok(CLIP, kui_paint_save(p));
    if (g_clip_on) ok(CLIP, kui_paint_clip_rounded_rect(p, 0.1f * w, 0.55f * h, 0.8f * w, 0.4f * h, &r24));
    ok(CLIP, kui_paint_fill_rect(p, 0, 0.5f * h, w, 0.5f * h, 0x14b8a6ffu));
    ok(CLIP, kui_paint_restore(p));
}

/* A 60x60 square rotated about (0.25w, 0.5h); a 10x10 square scaled x4 at (0.6w, 0.2h);
 * after the last restore a marker in the bottom-right corner (it lands there only if the
 * transforms were undone). */
static void paint_xform(KuiPaintContext* p, float w, float h) {
    ok(XFORM, kui_paint_save(p));
    ok(XFORM, kui_paint_translate(p, 0.25f * w, 0.5f * h));
    ok(XFORM, kui_paint_rotate(p, g_angle));
    ok(XFORM, kui_paint_fill_rect(p, -30, -30, 60, 60, 0x6366f1ffu));
    ok(XFORM, kui_paint_restore(p));
    ok(XFORM, kui_paint_save(p));
    ok(XFORM, kui_paint_translate(p, 0.6f * w, 0.2f * h));
    ok(XFORM, kui_paint_scale(p, 4, 4));
    ok(XFORM, kui_paint_fill_rect(p, 0, 0, 10, 10, 0xf59e0bffu));
    ok(XFORM, kui_paint_restore(p));
    ok(XFORM, kui_paint_fill_rect(p, w - 20, h - 20, 16, 16, 0x16a34affu));
    if (!g_unbalanced_tried) {
        /* kui_c.h: restore without a matching save is KUI_ERR_INVALID_ARG. */
        g_unbalanced_tried = 1;
        g_unbalanced = kui_paint_restore(p);
    }
}

/* Blue at g_alpha on the left; 0.5 inside 0.5 on the right. */
static void paint_alpha(KuiPaintContext* p, float w, float h) {
    ok(ALPHA, kui_paint_save(p));
    ok(ALPHA, kui_paint_set_opacity(p, g_alpha));
    ok(ALPHA, kui_paint_fill_rect(p, 0.05f * w, 0.15f * h, 0.4f * w, 0.7f * h, 0x0000ffffu));
    ok(ALPHA, kui_paint_restore(p));
    ok(ALPHA, kui_paint_save(p));
    ok(ALPHA, kui_paint_set_opacity(p, 0.5f));
    ok(ALPHA, kui_paint_save(p));
    ok(ALPHA, kui_paint_set_opacity(p, 0.5f));
    ok(ALPHA, kui_paint_fill_rect(p, 0.55f * w, 0.15f * h, 0.4f * w, 0.7f * h, 0x0000ffffu));
    ok(ALPHA, kui_paint_restore(p));
    ok(ALPHA, kui_paint_restore(p));
}

static void on_paint(KuiElement* element, KuiPaintContext* paint, void* user_data) {
    int pane = *(const int*)user_data;
    float w = 0, h = 0, dpr = 0;
    (void)element;
    ok(pane, kui_paint_get_info(paint, &w, &h, &dpr));
    g_paints[pane]++;
    g_width[pane] = w;
    g_height[pane] = h;
    g_dpr[pane] = dpr;
    switch (pane) {
        case SHAPES: paint_shapes(paint, w, h); break;
        case TEXT: paint_text(paint, w, h); break;
        case IMAGE: paint_image(paint, w, h); break;
        case CLIP: paint_clip(paint, w, h); break;
        case XFORM: paint_xform(paint, w, h); break;
        default: paint_alpha(paint, w, h); break;
    }
}

/* ---- Before the page loads: the image file and the six definitions ----------------------- */

static void put_u32(unsigned char* at, uint32_t value) {
    at[0] = (unsigned char)value;
    at[1] = (unsigned char)(value >> 8);
    at[2] = (unsigned char)(value >> 16);
    at[3] = (unsigned char)(value >> 24);
}

/* An 8x8 24-bit BMP filled with #14b8a6 (teal). */
static int write_image(void) {
    unsigned char header[54];
    unsigned char row[24];
    const char* dir = getenv("TEMP");
    FILE* file;
    int index;
    if (!dir || !*dir) dir = getenv("TMPDIR");
    if (!dir || !*dir) dir = "/tmp";
    snprintf(g_image_path, sizeof g_image_path, "%s/kui_api_paint_c.bmp", dir);
    memset(header, 0, sizeof header);
    header[0] = 'B';
    header[1] = 'M';
    put_u32(header + 2, 54 + 8 * 24);
    put_u32(header + 10, 54);
    put_u32(header + 14, 40);
    put_u32(header + 18, 8);
    put_u32(header + 22, 8);
    header[26] = 1;  /* planes */
    header[28] = 24; /* bits per pixel */
    put_u32(header + 34, 8 * 24);
    for (index = 0; index < 8; ++index) { /* blue, green, red */
        row[index * 3] = 0xa6;
        row[index * 3 + 1] = 0xb8;
        row[index * 3 + 2] = 0x14;
    }
    file = fopen(g_image_path, "wb");
    if (!file) return 0;
    fwrite(header, 1, sizeof header, file);
    for (index = 0; index < 8; ++index) fwrite(row, 1, sizeof row, file);
    fclose(file);
    return 1;
}

static KuiCCustomElementDefinition definition_for(int pane) {
    KuiCCustomElementDefinition definition;
    memset(&definition, 0, sizeof definition);
    definition.callbacks.user_data = &g_pane_index[pane];
    definition.paint = on_paint;
    return definition;
}

int kui_c_example_before_load(const KuiCExampleHost* host) {
    int pane;
    if (!write_image()) fprintf(stderr, "kui_api_paint: cannot write %s\n", g_image_path);
    for (pane = 0; pane < PANES; ++pane) {
        KuiCCustomElementDefinition definition = definition_for(pane);
        if (kui_document_define_custom_element_ex(host->document, g_tags[pane], 0, &definition) !=
            KUI_OK)
            return 1;
    }
    return 0;
}

/* ---- Self-check: definitions now, callbacks and repaints once frames have run ----------- */

static int g_marks[PANES];

static int bounding(int pane, KuiRect* rect) {
    KuiElement* element = kuex_by_id(g_doc, g_ids[pane]);
    int found = element && kui_element_get_bounding_rect(element, rect) == KUI_OK;
    if (element) kui_element_release(element);
    return found;
}

static void check_definitions(void) {
    KuiCCustomElementDefinition again = definition_for(SHAPES);
    KuiStatus status;
    int defined = 0;
    int pane;
    int all = 1;
    for (pane = 0; pane < PANES; ++pane) {
        defined = 0;
        kui_document_custom_element_is_defined(g_doc, g_tags[pane], 0, &defined);
        all = all && defined;
    }
    status = kui_document_define_custom_element_ex(g_doc, "paint-shapes", 0, &again);
    check("define_custom_element_ex", all && status == KUI_ERR_INVALID_ARG,
          "all defined %d, second definition status %d", all, status);
}

static void phase_repainted(void* user_data) {
    (void)user_data;
    check("request_repaint", g_paints[SHAPES] > g_marks[SHAPES], "paints %d -> %d",
          g_marks[SHAPES], g_paints[SHAPES]);
    check("invalidate_rect", g_paints[TEXT] > g_marks[TEXT], "paints %d -> %d", g_marks[TEXT],
          g_paints[TEXT]);
    finish();
}

static void phase_painted(void* user_data) {
    char sizes[256] = "";
    int pane;
    int all_painted = 1;
    int sizes_match = 1;
    int bad = 0;
    KuiElement* element;
    KuiTimerId id;
    (void)user_data;
    for (pane = 0; pane < PANES; ++pane) {
        KuiRect rect;
        size_t used = strlen(sizes);
        memset(&rect, 0, sizeof rect);
        bounding(pane, &rect);
        all_painted = all_painted && g_paints[pane] > 0;
        sizes_match = sizes_match && g_dpr[pane] > 0 && rect.width > 0 &&
                      g_width[pane] > rect.width - 0.5f && g_width[pane] < rect.width + 0.5f &&
                      g_height[pane] > rect.height - 0.5f && g_height[pane] < rect.height + 0.5f;
        snprintf(sizes + used, sizeof sizes - used, "%s%.0fx%.0f/%.0fx%.0f", pane ? " " : "",
                 g_width[pane], g_height[pane], rect.width, rect.height);
        bad += g_bad_calls[pane];
    }
    check("paint.callbacks_run", all_painted, "paints %d,%d,%d,%d,%d,%d", g_paints[0], g_paints[1],
          g_paints[2], g_paints[3], g_paints[4], g_paints[5]);
    check("paint_get_info.size", sizes_match, "info/element %s, dpr %.2f", sizes, g_dpr[0]);
    check("paint.calls_ok", bad == 0, "%d calls did not return KUI_OK", bad);
    check("paint_restore.unbalanced", g_unbalanced_tried && g_unbalanced == KUI_ERR_INVALID_ARG,
          "tried %d status %d", g_unbalanced_tried, g_unbalanced);

    for (pane = 0; pane < PANES; ++pane) g_marks[pane] = g_paints[pane];
    element = kuex_by_id(g_doc, "pv-shapes");
    if (element) {
        kui_element_request_repaint(element);
        kui_element_release(element);
    }
    element = kuex_by_id(g_doc, "pv-text");
    if (element) {
        kui_element_invalidate_rect(element, 0, 60, 300, 20);
        kui_element_release(element);
    }
    kui_platform_set_timeout(phase_repainted, NULL, 300, &id);
}

/* ---- Panel buttons: change what a pane draws, then ask for its repaint ------------------- */

static void repaint(int pane) {
    KuiElement* element = kuex_by_id(g_doc, g_ids[pane]);
    if (!element) return;
    kui_element_request_repaint(element);
    kui_element_release(element);
}

/* Repaints only the decoration line's band of the text pane. */
static void invalidate_line(void) {
    KuiElement* element = kuex_by_id(g_doc, "pv-text");
    float h = g_height[TEXT] > 0 ? g_height[TEXT] : 120.0f;
    float w = g_width[TEXT] > 0 ? g_width[TEXT] : 300.0f;
    if (!element) return;
    kui_element_invalidate_rect(element, 0, 0.6f * h - 2, w, 12);
    kui_element_release(element);
}

static void on_shapes_color(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    g_fill = g_fill == 0xef4444ffu ? 0xf97316ffu : 0xef4444ffu;
    repaint(SHAPES);
    kuex_set_text(g_doc, "shapes-out", g_fill == 0xef4444ffu ? "fill=#ef4444" : "fill=#f97316");
}

static void on_paint_info(KuiEvent* event, void* user_data) {
    char text[128];
    (void)event;
    (void)user_data;
    snprintf(text, sizeof text, "paints=%d,%d,%d,%d,%d,%d | %.0fx%.0f @%.2f", g_paints[0],
             g_paints[1], g_paints[2], g_paints[3], g_paints[4], g_paints[5], g_width[SHAPES],
             g_height[SHAPES], g_dpr[SHAPES]);
    kuex_set_text(g_doc, "shapes-out", text);
}

static void on_text_style(KuiEvent* event, void* user_data) {
    char text[48];
    (void)event;
    (void)user_data;
    g_line_style = (g_line_style + 1) % 5;
    invalidate_line();
    snprintf(text, sizeof text, "style=%s", g_style_names[g_line_style]);
    kuex_set_text(g_doc, "text-out", text);
}

static void on_text_color(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    g_line = g_line == 0xe11d48ffu ? 0x2563ebffu : 0xe11d48ffu;
    invalidate_line();
    kuex_set_text(g_doc, "text-out", g_line == 0xe11d48ffu ? "line=#e11d48" : "line=#2563eb");
}

static void on_image_rotate(KuiEvent* event, void* user_data) {
    static const char* const names[4] = {"#ff0000", "#00ff00", "#0000ff", "#ffff00"};
    char text[48];
    (void)event;
    (void)user_data;
    g_pixel_shift = (g_pixel_shift + 1) % 4;
    repaint(IMAGE);
    snprintf(text, sizeof text, "top-left=%s", names[g_pixel_shift]);
    kuex_set_text(g_doc, "image-out", text);
}

static void on_clip_toggle(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    g_clip_on = !g_clip_on;
    repaint(CLIP);
    kuex_set_text(g_doc, "clip-out", g_clip_on ? "clip=on" : "clip=off");
}

static void on_xform_rotate(KuiEvent* event, void* user_data) {
    char text[32];
    (void)event;
    (void)user_data;
    g_angle = g_angle == 0.0f ? 45.0f : 0.0f;
    repaint(XFORM);
    snprintf(text, sizeof text, "angle=%.0f", g_angle);
    kuex_set_text(g_doc, "xform-out", text);
}

static void on_alpha_cycle(KuiEvent* event, void* user_data) {
    char text[32];
    (void)event;
    (void)user_data;
    g_alpha = g_alpha == 0.5f ? 1.0f : (g_alpha == 1.0f ? 0.25f : 0.5f);
    repaint(ALPHA);
    snprintf(text, sizeof text, "opacity=%g", g_alpha);
    kuex_set_text(g_doc, "alpha-out", text);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    int failed = 0;
    KuiTimerId id;
    g_doc = host->document;

    check_definitions();
    /* Paint callbacks run with frames: check them once a few have been drawn. */
    kui_platform_set_timeout(phase_painted, NULL, 500, &id);

    failed |= kuex_on(g_doc, "btn-shapes-color", "click", on_shapes_color, NULL);
    failed |= kuex_on(g_doc, "btn-paint-info", "click", on_paint_info, NULL);
    failed |= kuex_on(g_doc, "btn-text-style", "click", on_text_style, NULL);
    failed |= kuex_on(g_doc, "btn-text-color", "click", on_text_color, NULL);
    failed |= kuex_on(g_doc, "btn-image-rotate", "click", on_image_rotate, NULL);
    failed |= kuex_on(g_doc, "btn-clip-toggle", "click", on_clip_toggle, NULL);
    failed |= kuex_on(g_doc, "btn-xform-rotate", "click", on_xform_rotate, NULL);
    failed |= kuex_on(g_doc, "btn-alpha-cycle", "click", on_alpha_cycle, NULL);
    return failed;
}
