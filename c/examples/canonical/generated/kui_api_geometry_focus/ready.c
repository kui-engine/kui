/* Copied by scripts/generate_c_examples.py from examples/c/ports/kui_api_geometry_focus.c; edit that file, not this copy. */
/* API tour: layout geometry, scrolling, anchored placement, text measurement, focus and pointer
 * capture (examples/api_tours/geometry_focus). On start the host calls every function once,
 * checks the numbers against the CSS on the page (and against each other: offset vs bounding
 * rect, scroll offset vs the content's moved rect) and logs one row per check into #tour-log;
 * #tour-result ends as "PASS n/n". The panel buttons repeat the calls on the visible boxes.
 *
 * Mouse input for the pointer-capture checks is injected with kui_devtools_dispatch_mouse_event,
 * which runs the same dispatch path as real input. */
#include "kui_c_example_dom.h"

#include <math.h>
#include <stdlib.h>
#include <stdarg.h>

static KuiDocument* g_doc;

/* ---- Self-check bookkeeping: count, log one <tr> per check, publish the verdict ----------
 * check():  a documented behaviour; a failure makes the tour FAIL.
 * known():  a documented behaviour the engine currently gets wrong (reported bug). A failure is
 *           logged as "KNOWN BUG" and kept out of the verdict; once it passes it counts as ok. */

static int g_total;
static int g_failed;
static int g_known;
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

static void known(const char* name, int ok, const char* bug, const char* fmt, ...) {
    char detail[256];
    char cell[400];
    va_list args;

    if (ok) {
        g_total++;
        log_row(name, "ok", "ok");
        return;
    }
    va_start(args, fmt);
    vsnprintf(detail, sizeof detail, fmt, args);
    va_end(args);
    g_known++;
    snprintf(cell, sizeof cell, "KNOWN BUG: %s (%s)", bug, detail);
    fprintf(stderr, "tour known bug: %s: %s\n", name, detail);
    log_row(name, "known", cell);
}

static void finish(void) {
    char text[640];
    char number[16];
    size_t used;
    KuiElement* result = kuex_by_id(g_doc, "tour-result");
    if (!result) return;
    if (g_failed == 0) {
        snprintf(text, sizeof text, "PASS %d/%d", g_total, g_total);
    } else {
        snprintf(text, sizeof text, "FAIL %d/%d: %s", g_failed, g_total, g_failed_names);
    }
    used = strlen(text);
    if (g_known) snprintf(text + used, sizeof text - used, " (known bugs: %d)", g_known);
    kui_element_set_text_content(result, text, 0);
    snprintf(number, sizeof number, "%d", g_total);
    kui_element_set_attribute(result, "data-total", 0, number, 0);
    snprintf(number, sizeof number, "%d", g_failed);
    kui_element_set_attribute(result, "data-failed", 0, number, 0);
    snprintf(number, sizeof number, "%d", g_known);
    kui_element_set_attribute(result, "data-known", 0, number, 0);
    kui_element_class_list_add(result, g_failed == 0 ? "pass" : "fail", 0);
    kui_element_release(result);
}

/* ---- Small readers ------------------------------------------------------------------------ */

typedef struct {
    float x, y, w, h;
} Box4;

static void release(KuiElement* element) {
    if (element) kui_element_release(element);
}

static int near(float a, float b, float tolerance) {
    return fabsf(a - b) <= tolerance;
}

static KuiRect rect_of(const char* id) {
    KuiRect rect;
    KuiElement* element = kuex_by_id(g_doc, id);
    memset(&rect, 0, sizeof rect);
    if (element) kui_element_get_bounding_rect(element, &rect);
    release(element);
    return rect;
}

static Box4 client_of(const char* id) {
    Box4 box = {0, 0, 0, 0};
    KuiElement* element = kuex_by_id(g_doc, id);
    if (element) kui_element_get_client_metrics(element, &box.x, &box.y, &box.w, &box.h);
    release(element);
    return box;
}

static Box4 offset_of(const char* id) {
    Box4 box = {0, 0, 0, 0};
    KuiElement* element = kuex_by_id(g_doc, id);
    if (element) kui_element_get_offset_metrics(element, &box.x, &box.y, &box.w, &box.h);
    release(element);
    return box;
}

static Box4 scroll_of(const char* id) {
    Box4 box = {0, 0, 0, 0};
    KuiElement* element = kuex_by_id(g_doc, id);
    if (element) kui_element_get_scroll_metrics(element, &box.x, &box.y, &box.w, &box.h);
    release(element);
    return box;
}

static int box_is(Box4 box, float x, float y, float w, float h) {
    return near(box.x, x, 0.01f) && near(box.y, y, 0.01f) && near(box.w, w, 0.01f) &&
           near(box.h, h, 0.01f);
}

static int focused(const char* id) {
    int yes = -1;
    KuiElement* element = kuex_by_id(g_doc, id);
    if (element) kui_element_has_focus(element, &yes);
    release(element);
    return yes;
}

static int captured(const char* id, int pointer_id) {
    int yes = -1;
    KuiElement* element = kuex_by_id(g_doc, id);
    if (element) kui_element_has_pointer_capture(element, pointer_id, &yes);
    release(element);
    return yes;
}

/* Injects one mouse event at the centre of an element (viewport coordinates). */
static void mouse_at(const char* id, KuiCMouseEventType type, KuiCMouseButton button) {
    KuiRect rect = rect_of(id);
    kui_devtools_dispatch_mouse_event(g_doc, type, rect.x + rect.width / 2,
                                      rect.y + rect.height / 2, button, 0,
                                      type == KUI_C_MOUSE_MOVED ? 0 : 1, 0, 0);
}

/* ---- Box metrics: bounding rect, client (padding box), offset (border box vs offset parent) -- */

static void check_box_metrics(void) {
    KuiRect frame = rect_of("geo-frame");
    KuiRect box = rect_of("geo-box");
    KuiRect bb = rect_of("geo-bb");
    Box4 client_box = client_of("geo-box");
    Box4 client_bb = client_of("geo-bb");
    Box4 offset_box = offset_of("geo-box");
    Box4 offset_bb = offset_of("geo-bb");

    /* content-box: 200 + 2*12 padding + 2*6 border; border-box: the declared 200x50. */
    check("geo.bounding_rect-size",
          near(box.width, 236, 0.5f) && near(box.height, 76, 0.5f) && near(bb.width, 200, 0.5f) &&
          near(bb.height, 50, 0.5f), "content-box %.1fx%.1f (want 236x76), border-box %.1fx%.1f "
          "(want 200x50)", box.width, box.height, bb.width, bb.height);
    /* Frame border 2 + frame padding 8 + box margin 10 from the frame's edge. */
    check("geo.bounding_rect-position",
          near(box.x - frame.x, 20, 0.5f) && near(box.y - frame.y, 20, 0.5f),
          "box at +%.1f,+%.1f from the frame (want +20,+20)", box.x - frame.x, box.y - frame.y);
    /* Padding box: left/top are the border widths. */
    check("geo.client_metrics", box_is(client_box, 6, 6, 224, 64) && box_is(client_bb, 4, 4, 192, 42),
          "content-box %.0f,%.0f %.0fx%.0f (want 6,6 224x64), border-box %.0f,%.0f %.0fx%.0f "
          "(want 4,4 192x42)", client_box.x, client_box.y, client_box.w, client_box.h,
          client_bb.x, client_bb.y, client_bb.w, client_bb.h);
    /* Border box from the offset parent's padding edge; the boxes' 10px margins collapse. */
    check("geo.offset_metrics", box_is(offset_box, 18, 18, 236, 76) && box_is(offset_bb, 18, 104, 200, 50),
          "content-box %.0f,%.0f %.0fx%.0f (want 18,18 236x76), border-box %.0f,%.0f %.0fx%.0f "
          "(want 18,104 200x50)", offset_box.x, offset_box.y, offset_box.w, offset_box.h,
          offset_bb.x, offset_bb.y, offset_bb.w, offset_bb.h);
    /* offsetLeft/Top = the bounding rects' distance minus the parent's border, rounded. */
    check("geo.offset_vs_rect",
          offset_bb.x == floorf(bb.x - frame.x - 2 + 0.5f) &&
          offset_bb.y == floorf(bb.y - frame.y - 2 + 0.5f) &&
          offset_bb.w == floorf(bb.width + 0.5f) && offset_bb.h == floorf(bb.height + 0.5f),
          "offset %.0f,%.0f vs rects %.2f,%.2f", offset_bb.x, offset_bb.y, bb.x - frame.x - 2,
          bb.y - frame.y - 2);
}

/* ---- Scrolling: extent, set_scroll_position, scroll_by, clamping ------------------------- */

static void scroll_to(float left, float top) {
    KuiElement* scroller = kuex_by_id(g_doc, "scroller");
    if (scroller) kui_element_set_scroll_position(scroller, left, top);
    release(scroller);
}

static void scroll_by(float dx, float dy) {
    KuiElement* scroller = kuex_by_id(g_doc, "scroller");
    if (scroller) kui_element_scroll_by(scroller, dx, dy);
    release(scroller);
}

static void check_scrolling(void) {
    KuiRect outer = rect_of("scroller");
    KuiRect row0 = rect_of("row-0");
    KuiRect moved;
    Box4 client = client_of("scroller");
    Box4 scroll = scroll_of("scroller");

    check("scroll.extent", box_is(scroll, 0, 0, 600, 400), "scroll %.1f,%.1f %.0fx%.0f (want 0,0 600x400)",
          scroll.x, scroll.y, scroll.w, scroll.h);
    /* 1px border; a rendered scrollbar may take some of the padding box. */
    check("scroll.client", client.x == 1 && client.y == 1 && client.w <= outer.width - 2 &&
          client.w > outer.width - 24 && client.h <= 118 && client.h > 96,
          "client %.0f,%.0f %.0fx%.0f of a %.0fx%.0f box", client.x, client.y, client.w, client.h,
          outer.width, outer.height);

    scroll_to(0, 100);
    scroll = scroll_of("scroller");
    moved = rect_of("row-0");
    check("scroll.set_scroll_position", near(scroll.y, 100, 0.01f) && near(scroll.x, 0, 0.01f) &&
          near(row0.y - moved.y, 100, 0.5f), "scroll %.1f,%.1f, row 0 moved up %.1f",
          scroll.x, scroll.y, row0.y - moved.y);

    scroll_by(30, 50);
    scroll = scroll_of("scroller");
    moved = rect_of("row-0");
    check("scroll.scroll_by", near(scroll.x, 30, 0.01f) && near(scroll.y, 150, 0.01f) &&
          near(row0.x - moved.x, 30, 0.5f) && near(row0.y - moved.y, 150, 0.5f),
          "scroll %.1f,%.1f, row 0 moved %.1f,%.1f", scroll.x, scroll.y, row0.x - moved.x,
          row0.y - moved.y);

    /* Past the end clamps to extent - client size. */
    scroll_to(10000, 10000);
    scroll = scroll_of("scroller");
    check("scroll.clamp", near(scroll.x, 600 - client.w, 0.5f) && near(scroll.y, 400 - client.h, 0.5f),
          "scroll %.1f,%.1f (want %.0f,%.0f)", scroll.x, scroll.y, 600 - client.w, 400 - client.h);

    scroll_to(0, 0);
    scroll = scroll_of("scroller");
    moved = rect_of("row-0");
    check("scroll.reset", near(scroll.x, 0, 0.01f) && near(scroll.y, 0, 0.01f) &&
          near(moved.y, row0.y, 0.5f), "scroll %.1f,%.1f", scroll.x, scroll.y);
}

/* ---- Scroll into view and inertial scrolling ------------------------------------------------ */

static void check_scroll_reveal(void) {
    KuiElement* scroller = kuex_by_id(g_doc, "scroller");
    KuiElement* last = kuex_by_id(g_doc, "row-9");
    Box4 scroll;
    KuiRect box, row;
    KuiStatus status;
    float previous = -1.0f, now_y = 0.0f;
    int i;

    if (!scroller || !last) {
        check("reveal.setup", 0, "missing #scroller or #row-9");
        if (scroller) kui_element_release(scroller);
        if (last) kui_element_release(last);
        return;
    }
    scroll_to(0, 0);
    kui_element_scroll_into_view(last, 1);
    scroll = scroll_of("scroller");
    box = rect_of("scroller");
    row = rect_of("row-9");
    /* The last row is now inside the scroll box (1px border). */
    check("scroll.scroll_into_view",
          scroll.y > 100 && row.y >= box.y && row.y + row.height <= box.y + box.height + 1,
          "scroll %.1f,%.1f, row 9 at %.1f+%.1f in a box at %.1f+%.1f", scroll.x, scroll.y, row.y, row.height,
          box.y, box.height);

    /* Inertial scroll: a velocity (px/ms) that decays frame by frame until the container stops. */
    scroll_to(0, 0);
    status = kui_element_start_momentum_scroll(scroller, 0.0f, 0.1f);
    for (i = 0; i < 80; ++i) {
        kui_platform_wait_events(0.03);
        now_y = scroll_of("scroller").y;
        if (now_y > 0 && now_y == previous) break;
        previous = now_y;
    }
    check("scroll.start_momentum_scroll", status == KUI_OK && now_y > 0 && now_y < 200,
          "status %d, stopped at %.1f", (int)status, now_y);
    scroll_to(0, 0);
    kui_element_release(scroller);
    kui_element_release(last);
}


/* ---- Anchored placement: position_relative_to with placements, flip and clamp ------------ */

static void place(const char* placement, float offset, int flip, int clamp, float margin) {
    KuiElement* popup = kuex_by_id(g_doc, "pos-popup");
    KuiElement* anchor = kuex_by_id(g_doc, "pos-anchor");
    KuiCPositioningOptions options;
    options.flip = flip;
    options.clamp_to_viewport = clamp;
    options.offset = offset;
    options.viewport_margin = margin;
    if (popup && anchor) kui_element_position_relative_to(popup, anchor, placement, 0, &options);
    release(popup);
    release(anchor);
}

static void check_placement(void) {
    KuiRect a = rect_of("pos-anchor");
    KuiRect p;
    char left[32];
    size_t length = 0;

    place("bottom-start", 6, 0, 0, 0);
    p = rect_of("pos-popup");
    check("pos.bottom-start", near(p.x, a.x, 0.5f) && near(p.y, a.y + a.height + 6, 0.5f),
          "popup %.1f,%.1f anchor %.1f,%.1f %.0fx%.0f", p.x, p.y, a.x, a.y, a.width, a.height);
    {
        KuiElement* popup = kuex_by_id(g_doc, "pos-popup");
        left[0] = '\0';
        if (popup) kui_element_get_style_property(popup, "left", 0, left, sizeof left, &length);
        release(popup);
        check("pos.inline-left", length > 2 && strcmp(left + length - 2, "px") == 0 &&
              near((float)atof(left), a.x, 0.5f), "inline left '%s'", left);
    }

    place("right-center", 4, 0, 0, 0);
    p = rect_of("pos-popup");
    check("pos.right-center", near(p.x, a.x + a.width + 4, 0.5f) &&
          near(p.y + p.height / 2, a.y + a.height / 2, 0.5f),
          "popup %.1f,%.1f %.0fx%.0f", p.x, p.y, p.width, p.height);

    place("left-end", 8, 0, 0, 0);
    p = rect_of("pos-popup");
    check("pos.left-end", near(p.x + p.width, a.x - 8, 0.5f) &&
          near(p.y + p.height, a.y + a.height, 0.5f), "popup %.1f,%.1f %.0fx%.0f",
          p.x, p.y, p.width, p.height);

    /* "top" with a viewport margin below the anchor's top cannot fit: flip puts it below. */
    place("top", 5, 1, 0, a.y);
    p = rect_of("pos-popup");
    check("pos.flip", near(p.y, a.y + a.height + 5, 0.5f), "popup y %.1f (want %.1f)", p.y,
          a.y + a.height + 5);

    /* Far to the left with clamping: held at the viewport margin. */
    place("left", 2000, 0, 1, 7);
    p = rect_of("pos-popup");
    check("pos.clamp", near(p.x, 7, 0.5f), "popup x %.1f (want 7)", p.x);

    place("bottom-start", 6, 1, 1, 4);
}

/* ---- Text measurement --------------------------------------------------------------------- */

static KuiCTextMetrics measure(const char* text, const char* family, float size, int weight) {
    KuiCTextMetrics metrics;
    KuiCTextStyle style;
    KuiElement* sample = kuex_by_id(g_doc, "measure-sample");
    memset(&metrics, 0, sizeof metrics);
    memset(&style, 0, sizeof style);
    style.font_family = family;
    style.font_size = size;
    style.font_weight = (uint16_t)weight;
    if (sample) kui_element_measure_text(sample, text, 0, &style, &metrics);
    release(sample);
    return metrics;
}

static void check_measure(void) {
    KuiCTextMetrics small = measure("Hello KUI", NULL, 12, 400);
    KuiCTextMetrics mid = measure("Hello KUI", NULL, 16, 400);
    KuiCTextMetrics large = measure("Hello KUI", NULL, 24, 400);
    KuiCTextMetrics empty = measure("", NULL, 16, 400);
    KuiCTextMetrics segoe = measure("Hello KUI", "Segoe UI", 16, 400);
    KuiRect span = rect_of("measure-span");

    check("measure.monotonic", small.width > 0 && small.width < mid.width && mid.width < large.width,
          "widths %.2f / %.2f / %.2f at 12 / 16 / 24px", small.width, mid.width, large.width);
    check("measure.metrics", mid.ascent > 0 && mid.descent > 0 &&
          mid.line_height >= mid.ascent + mid.descent - 0.5f && large.ascent > mid.ascent,
          "ascent %.2f descent %.2f line height %.2f", mid.ascent, mid.descent, mid.line_height);
    check("measure.empty", empty.width == 0 && near(empty.ascent, mid.ascent, 0.01f),
          "empty width %.2f ascent %.2f", empty.width, empty.ascent);
    /* Same shaping as layout: the inline-block span with the same text, font and size. */
    check("measure.matches_layout", near(segoe.width, span.width, 1.0f),
          "measured %.2f, laid-out span %.2f", segoe.width, span.width);
}

/* ---- Focus: focus / blur / has_focus, and the focus / blur events they fire --------------- */

static int g_focus_events;
static int g_blur_events;

static void on_div_focus(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    g_focus_events++;
}

static void on_input_blur(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    g_blur_events++;
}

static void focus_id(const char* id) {
    KuiElement* element = kuex_by_id(g_doc, id);
    if (element) kui_element_focus(element);
    release(element);
}

static void blur_id(const char* id) {
    KuiElement* element = kuex_by_id(g_doc, id);
    if (element) kui_element_blur(element);
    release(element);
}

static void check_focus(void) {
    kuex_on(g_doc, "f-div", "focus", on_div_focus, NULL);
    kuex_on(g_doc, "f-input", "blur", on_input_blur, NULL);

    focus_id("f-btn");
    check("focus.button", focused("f-btn") == 1, "has_focus(button) = %d", focused("f-btn"));

    focus_id("f-input");
    check("focus.moves", focused("f-input") == 1 && focused("f-btn") == 0,
          "input %d, button %d", focused("f-input"), focused("f-btn"));

    blur_id("f-input");
    check("focus.blur", focused("f-input") == 0 && g_blur_events == 1,
          "has_focus(input) = %d, blur events %d", focused("f-input"), g_blur_events);

    focus_id("f-div");
    check("focus.tabindex", focused("f-div") == 1 && g_focus_events == 1,
          "has_focus(div) = %d, focus events %d", focused("f-div"), g_focus_events);

    /* A div without tabindex is not focusable (HTML focus()): focus() changes nothing. */
    focus_id("f-plain");
    known("focus.not_focusable", focused("f-plain") == 0 && focused("f-div") == 1,
          "focus() also focuses an element that is not focusable (div without tabindex)",
          "plain %d, tabindex div %d", focused("f-plain"), focused("f-div"));

    blur_id("f-div");
}

/* ---- Pointer capture: set / has / release, the capture events, routing, implicit release - */

static int g_got_capture;
static int g_lost_capture;
static int g_captured_moves;
static int g_capture_target; /* 1 when gotpointercapture's current target was #cap-box */

static int is_cap_box(KuiElement* element) {
    char id[32];
    size_t length = 0;
    id[0] = '\0';
    if (element) kui_element_get_id(element, id, sizeof id, &length);
    return element && strcmp(id, "cap-box") == 0;
}

static void on_got_capture(KuiEvent* event, void* user_data) {
    KuiElement* current = NULL;
    (void)user_data;
    g_got_capture++;
    kui_event_get_current_target(event, &current);
    g_capture_target = is_cap_box(current);
    release(current);
    kuex_set_class(g_doc, "cap-box", "captured");
}

static void on_lost_capture(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    g_lost_capture++;
    kuex_set_class(g_doc, "cap-box", "");
}

static void on_captured_move(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    g_captured_moves++;
}

static void capture(int on) {
    KuiElement* box = kuex_by_id(g_doc, "cap-box");
    if (box && on) kui_element_set_pointer_capture(box, 1);
    if (box && !on) kui_element_release_pointer_capture(box, 1);
    release(box);
}

static void check_capture(void) {
    int moves;
    kuex_on(g_doc, "cap-box", "gotpointercapture", on_got_capture, NULL);
    kuex_on(g_doc, "cap-box", "lostpointercapture", on_lost_capture, NULL);
    kuex_on(g_doc, "cap-box", "pointermove", on_captured_move, NULL);

    check("cap.initial", captured("cap-box", 1) == 0, "has_pointer_capture(1) = %d",
          captured("cap-box", 1));

    capture(1);
    check("cap.set", captured("cap-box", 1) == 1 && captured("cap-box", 2) == 0 && g_got_capture == 1,
          "pointer 1 %d, pointer 2 %d, gotpointercapture %d", captured("cap-box", 1),
          captured("cap-box", 2), g_got_capture);
    /* The capture events are dispatched at the capturing element. */
    known("cap.event_target", g_capture_target == 1,
          "gotpointercapture / lostpointercapture have no target or current target",
          "current target is #cap-box: %d", g_capture_target);

    /* A move over another element is delivered to the capturing box. */
    moves = g_captured_moves;
    mouse_at("cap-other", KUI_C_MOUSE_MOVED, KUI_C_MOUSE_BUTTON_NONE);
    check("cap.routes_moves", g_captured_moves == moves + 1, "pointermove on the box %d -> %d",
          moves, g_captured_moves);

    capture(0);
    check("cap.release", captured("cap-box", 1) == 0 && g_lost_capture == 1,
          "has_pointer_capture(1) = %d, lostpointercapture %d", captured("cap-box", 1),
          g_lost_capture);

    /* Releasing the button ends a capture by itself (implicit release). */
    capture(1);
    mouse_at("cap-other", KUI_C_MOUSE_PRESSED, KUI_C_MOUSE_BUTTON_LEFT);
    mouse_at("cap-other", KUI_C_MOUSE_RELEASED, KUI_C_MOUSE_BUTTON_LEFT);
    check("cap.implicit_release", captured("cap-box", 1) == 0 && g_lost_capture == 2,
          "has_pointer_capture(1) = %d, lostpointercapture %d", captured("cap-box", 1),
          g_lost_capture);
    mouse_at("cap-other", KUI_C_MOUSE_MOVED, KUI_C_MOUSE_BUTTON_NONE);
}

/* ---- Panel buttons ----------------------------------------------------------------------- */

static int g_wide_padding;

static void show_geo(void) {
    Box4 client = client_of("geo-box");
    Box4 offset = offset_of("geo-box");
    KuiRect rect = rect_of("geo-box");
    char out[160];
    snprintf(out, sizeof out, "rect %.0fx%.0f | client %.0f,%.0f %.0fx%.0f | offset %.0f,%.0f",
             rect.width, rect.height, client.x, client.y, client.w, client.h, offset.x, offset.y);
    kuex_set_text(g_doc, "geo-out", out);
}

static void on_geo_show(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    show_geo();
}

static void on_geo_pad(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    g_wide_padding = !g_wide_padding;
    kuex_set_style(g_doc, "geo-box", "padding", g_wide_padding ? "20px" : "12px");
    show_geo();
}

static void show_scroll(void) {
    Box4 scroll = scroll_of("scroller");
    char out[64];
    snprintf(out, sizeof out, "left=%.0f top=%.0f", scroll.x, scroll.y);
    kuex_set_text(g_doc, "scroll-out", out);
}

static void on_scroll_down(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    scroll_by(0, 40);
    show_scroll();
}

static void on_scroll_right(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    scroll_by(50, 0);
    show_scroll();
}

static void on_scroll_end(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    scroll_to(10000, 10000);
    show_scroll();
}

static void on_scroll_top(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    scroll_to(0, 0);
    show_scroll();
}

static void show_pos(const char* placement) {
    KuiRect a = rect_of("pos-anchor");
    KuiRect p = rect_of("pos-popup");
    char out[96];
    snprintf(out, sizeof out, "%s: dx=%.0f dy=%.0f", placement, p.x - a.x, p.y - a.y);
    kuex_set_text(g_doc, "pos-out", out);
}

static void on_pos_below(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    place("bottom-start", 6, 1, 1, 4);
    show_pos("bottom-start");
}

static void on_pos_right(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    place("right-center", 6, 1, 1, 4);
    show_pos("right-center");
}

static void on_pos_left(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    place("left-end", 6, 1, 1, 4);
    show_pos("left-end");
}

static int g_measure_size = 16;
static int g_measure_bold;

static void show_measure(void) {
    KuiCTextMetrics metrics = measure("Hello KUI", "Segoe UI", (float)g_measure_size,
                                      g_measure_bold ? 700 : 400);
    char value[16];
    char out[128];
    snprintf(value, sizeof value, "%dpx", g_measure_size);
    kuex_set_style(g_doc, "measure-sample", "font-size", value);
    kuex_set_style(g_doc, "measure-sample", "font-weight", g_measure_bold ? "700" : "400");
    snprintf(out, sizeof out, "size=%d%s width=%.1f ascent=%.1f descent=%.1f line=%.1f",
             g_measure_size, g_measure_bold ? " bold" : "", metrics.width, metrics.ascent,
             metrics.descent, metrics.line_height);
    kuex_set_text(g_doc, "measure-out", out);
}

static void on_measure_smaller(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    if (g_measure_size > 8) g_measure_size -= 4;
    show_measure();
}

static void on_measure_larger(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    if (g_measure_size < 40) g_measure_size += 4;
    show_measure();
}

static void on_measure_bold(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    g_measure_bold = !g_measure_bold;
    show_measure();
}

static void show_focus(const char* note) {
    char out[128];
    snprintf(out, sizeof out, "%s | button=%d input=%d div=%d plain=%d", note, focused("f-btn"),
             focused("f-input"), focused("f-div"), focused("f-plain"));
    kuex_set_text(g_doc, "focus-out", out);
}

static void on_focus_input(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    focus_id("f-input");
    show_focus("focus(input)");
}

static void on_focus_div(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    focus_id("f-div");
    show_focus("focus(div)");
}

static void on_focus_plain(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    focus_id("f-plain");
    show_focus("focus(plain)");
}

static void on_focus_blur(KuiEvent* event, void* user_data) {
    char note[48];
    int before;
    (void)event;
    (void)user_data;
    focus_id("f-input");
    before = focused("f-input");
    blur_id("f-input");
    snprintf(note, sizeof note, "focus+blur(input) %d->%d", before, focused("f-input"));
    show_focus(note);
}

static void show_capture(void) {
    char out[96];
    snprintf(out, sizeof out, "captured=%s | got=%d lost=%d moves=%d",
             captured("cap-box", 1) == 1 ? "yes" : "no", g_got_capture, g_lost_capture,
             g_captured_moves);
    kuex_set_text(g_doc, "cap-out", out);
}

static void on_cap_set(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    capture(1);
    show_capture();
}

static void on_cap_release(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    capture(0);
    show_capture();
}

static void on_cap_state(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    show_capture();
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    int failed = 0;
    g_doc = host->document;

    check_box_metrics();
    check_scrolling();
    check_scroll_reveal();
    check_placement();
    check_measure();
    check_focus();
    check_capture();
    finish();

    failed |= kuex_on(g_doc, "btn-geo-show", "click", on_geo_show, NULL);
    failed |= kuex_on(g_doc, "btn-geo-pad", "click", on_geo_pad, NULL);
    failed |= kuex_on(g_doc, "btn-scroll-down", "click", on_scroll_down, NULL);
    failed |= kuex_on(g_doc, "btn-scroll-right", "click", on_scroll_right, NULL);
    failed |= kuex_on(g_doc, "btn-scroll-end", "click", on_scroll_end, NULL);
    failed |= kuex_on(g_doc, "btn-scroll-top", "click", on_scroll_top, NULL);
    failed |= kuex_on(g_doc, "btn-pos-below", "click", on_pos_below, NULL);
    failed |= kuex_on(g_doc, "btn-pos-right", "click", on_pos_right, NULL);
    failed |= kuex_on(g_doc, "btn-pos-left", "click", on_pos_left, NULL);
    failed |= kuex_on(g_doc, "btn-measure-smaller", "click", on_measure_smaller, NULL);
    failed |= kuex_on(g_doc, "btn-measure-larger", "click", on_measure_larger, NULL);
    failed |= kuex_on(g_doc, "btn-measure-bold", "click", on_measure_bold, NULL);
    failed |= kuex_on(g_doc, "btn-focus-input", "click", on_focus_input, NULL);
    failed |= kuex_on(g_doc, "btn-focus-div", "click", on_focus_div, NULL);
    failed |= kuex_on(g_doc, "btn-focus-plain", "click", on_focus_plain, NULL);
    failed |= kuex_on(g_doc, "btn-focus-blur", "click", on_focus_blur, NULL);
    failed |= kuex_on(g_doc, "btn-cap-set", "click", on_cap_set, NULL);
    failed |= kuex_on(g_doc, "btn-cap-release", "click", on_cap_release, NULL);
    failed |= kuex_on(g_doc, "btn-cap-state", "click", on_cap_state, NULL);
    show_measure();
    return failed;
}
