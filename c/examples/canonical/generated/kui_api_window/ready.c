/* Copied by scripts/generate_c_examples.py from examples/c/ports/kui_api_window.c; edit that file, not this copy. */
/* API tour: the native window (examples/api_tours/window). The host changes title, size and
 * position, listens for resizes (callback and "resize" event listeners), cycles maximize and
 * fullscreen, shapes the frame, sets opacity / z-order / cursor / IME position, installs a native
 * menu bar, binds keyboard shortcuts, drives a hidden tray icon with simulated clicks, reads the
 * DevTools state and saves a screenshot -- then checks the window is back exactly as it was.
 * One #tour-log row per check; #tour-result ends as "PASS n/n". Minimize, stop DevTools and the
 * tray context menu are only behind (grey) buttons: they would stop rendering, cut the
 * inspector's connection or open a system menu. */
#include "kui_c_example_dom.h"

#include <stdarg.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static KuiDocument* g_doc;
static KuiWindow* g_window;
static KuiScriptEngine* g_script;

/* ---- Self-check bookkeeping: count, log one <tr> per check, publish the verdict ----------
 * check():  a documented behaviour; a failure makes the tour FAIL.
 * known():  a documented behaviour the engine currently gets wrong (reported bug).
 * skip():   not available on this platform; logged, not counted. */

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
        if (strcmp(result, "ok") != 0 && strcmp(result, "skip") != 0) {
            kui_element_class_list_add(row, "bad", 0);
        }
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
    char cell[512];
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

static void skip(const char* name, const char* why) {
    char cell[256];
    snprintf(cell, sizeof cell, "skipped: %s", why);
    log_row(name, "skip", cell);
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

/* ---- Small readers ------------------------------------------------------------------------- */

typedef struct Geometry {
    int width, height, x, y;
} Geometry;

static Geometry geometry(void) {
    Geometry g;
    memset(&g, 0, sizeof g);
    kui_window_get_size(g_window, &g.width, &g.height);
    kui_window_get_position(g_window, &g.x, &g.y);
    return g;
}

static void window_title(char* buf, size_t capacity) {
    size_t length = 0;
    buf[0] = '\0';
    kui_window_get_title(g_window, buf, capacity, &length);
}

static int flag(KuiStatus (*getter)(KuiWindow*, int*)) {
    int value = -1;
    getter(g_window, &value);
    return value;
}

static double lua_number(const char* code) {
    char text[64];
    kuex_eval_text(g_script, code, text, sizeof text);
    return text[0] ? atof(text) : -1.0;
}

static int near_f(float a, float b) { return a - b < 0.01f && b - a < 0.01f; }

/* The state the self-check must leave behind. */
static char g_orig_title[256];
static Geometry g_orig;
static int g_orig_frameless, g_orig_shadow, g_orig_sizeable;

/* Sends a real key-down / key-up to the native window -- the path a keyboard takes, where window
 * shortcuts are matched. Windows only; returns 0 elsewhere. */
static int send_native_key(unsigned virtual_key) {
#ifdef _WIN32
    void* handle = NULL;
    if (kui_window_get_native_handle(g_window, &handle) != KUI_OK || !handle) return 0;
    SendMessageW((HWND)handle, WM_KEYDOWN, (WPARAM)virtual_key, (LPARAM)1);
    SendMessageW((HWND)handle, WM_KEYUP, (WPARAM)virtual_key, (LPARAM)0xC0000001u);
    return 1;
#else
    (void)virtual_key;
    return 0;
#endif
}

#ifdef _WIN32
#define KEY_F7 VK_F7
#else
#define KEY_F7 0x76u
#endif

/* ---- Title, size, position, resize notifications ------------------------------------------ */

static int g_resize_calls;
static uint32_t g_resize_w, g_resize_h;
static int g_events_a, g_events_b;

static void show_resize(void) {
    char text[160];
    snprintf(text, sizeof text, "on_resize %ux%u (callbacks=%d)", (unsigned)g_resize_w,
             (unsigned)g_resize_h, g_resize_calls);
    kuex_set_text(g_doc, "resize-out", text);
}

static void on_resize(uint32_t width, uint32_t height, void* user_data) {
    (void)user_data;
    g_resize_calls++;
    g_resize_w = width;
    g_resize_h = height;
    show_resize();
}

static void on_resize_event_a(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    g_events_a++;
}

static void on_resize_event_b(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    g_events_b++;
}

static void check_geometry(void) {
    char title[256];
    char small[4];
    size_t length = 0;
    void* handle = NULL;
    float ratio = 0.0f;
    double inner_width, lua_ratio;
    KuiListenerId listener_a = 0, listener_b = 0;
    Geometry g;
    int a, b, calls;
    KuiStatus status;
    const char* host_title = "kui_api_window: title set by the host";

    check("win.get_title", strcmp(g_orig_title, "KUI API \xe2\x80\x94 Window") == 0, "title '%s'",
          g_orig_title);
    status = kui_window_get_title(g_window, small, sizeof small, &length);
    check("win.get_title-buffer-too-small",
          status == KUI_ERR_BUFFER_TOO_SMALL && length == strlen(g_orig_title),
          "status %d length %u", status, (unsigned)length);
    status = kui_window_set_title(g_window, host_title, 0);
    window_title(title, sizeof title);
    check("win.set_title", status == KUI_OK && strcmp(title, host_title) == 0, "status %d title '%s'",
          status, title);
    kui_window_set_title(g_window, g_orig_title, 0);
    window_title(title, sizeof title);
    check("win.set_title-restore", strcmp(title, g_orig_title) == 0, "title '%s'", title);

    status = kui_window_get_device_pixel_ratio(g_window, &ratio);
    lua_ratio = lua_number("return tostring(window.devicePixelRatio())");
    check("win.get_device_pixel_ratio", status == KUI_OK && ratio > 0.0f &&
                                            near_f(ratio, (float)lua_ratio),
          "status %d ratio %.3f, Lua window.devicePixelRatio() %.3f", status, ratio, lua_ratio);
    /* get_size is the client area in device pixels; the page sees it in CSS px. */
    inner_width = lua_number("return tostring(window.innerWidth())");
    check("win.get_size", g_orig.width > 0 && g_orig.height > 0 &&
                              inner_width * ratio - g_orig.width < 1.5 &&
                              g_orig.width - inner_width * ratio < 1.5,
          "size %dx%d, innerWidth %.1f x ratio %.2f", g_orig.width, g_orig.height, inner_width,
          ratio);
    status = kui_window_get_native_handle(g_window, &handle);
#ifdef _WIN32
    check("win.get_native_handle", status == KUI_OK && handle && IsWindow((HWND)handle),
          "status %d handle %p", status, handle);
#else
    check("win.get_native_handle", status == KUI_OK && handle, "status %d handle %p", status,
          handle);
#endif

    /* Two "resize" listeners and the resize callback, then remove them one by one. */
    status = kui_window_on_resize(g_window, on_resize, NULL);
    kui_window_add_event_listener(g_window, "resize", 0, on_resize_event_a, NULL, 0, &listener_a);
    kui_window_add_event_listener(g_window, "resize", 0, on_resize_event_b, NULL, 0, &listener_b);
    status = kui_window_resize(g_window, g_orig.width - 60, g_orig.height - 40);
    g = geometry();
    check("win.resize", status == KUI_OK && g.width == g_orig.width - 60 &&
                            g.height == g_orig.height - 40,
          "status %d size %dx%d, wanted %dx%d", status, g.width, g.height, g_orig.width - 60,
          g_orig.height - 40);
    check("win.on_resize", g_resize_calls > 0 && (int)g_resize_w == g.width &&
                               (int)g_resize_h == g.height,
          "%d callback(s), last %ux%u", g_resize_calls, (unsigned)g_resize_w,
          (unsigned)g_resize_h);
    check("win.add_event_listener-resize", g_events_a > 0 && g_events_b > 0,
          "listener a %d, b %d", g_events_a, g_events_b);

    a = g_events_a;
    b = g_events_b;
    status = kui_window_remove_event_listener(g_window, "resize", 0, listener_a);
    kui_window_resize(g_window, g_orig.width - 30, g_orig.height - 20);
    check("win.remove_event_listener", status == KUI_OK && g_events_a == a && g_events_b > b,
          "status %d, removed listener %d -> %d, kept listener %d -> %d", status, a, g_events_a,
          b, g_events_b);
    a = g_events_a;
    b = g_events_b;
    calls = g_resize_calls;
    status = kui_window_remove_event_listeners(g_window, "resize", 0);
    kui_window_resize(g_window, g_orig.width, g_orig.height);
    g = geometry();
    check("win.remove_event_listeners", status == KUI_OK && g_events_a == a && g_events_b == b &&
                                            g_resize_calls > calls,
          "status %d, listeners %d/%d -> %d/%d, callback %d -> %d", status, a, b, g_events_a,
          g_events_b, calls, g_resize_calls);
    check("win.resize-back", g.width == g_orig.width && g.height == g_orig.height, "size %dx%d",
          g.width, g.height);

    status = kui_window_set_position(g_window, g_orig.x + 24, g_orig.y + 16);
    g = geometry();
    check("win.get_position", status == KUI_OK && g.x == g_orig.x + 24 && g.y == g_orig.y + 16,
          "status %d moved to (%d, %d), wanted (%d, %d)", status, g.x, g.y, g_orig.x + 24,
          g_orig.y + 16);
    kui_window_set_position(g_window, g_orig.x, g_orig.y);
}

/* ---- Maximize / restore, fullscreen, focus, close flag ------------------------------------ */

static void check_state(void) {
    Geometry g;
    KuiStatus status;
    int focused = -1;

    check("state.is_minimized", flag(kui_window_is_minimized) == 0, "is_minimized %d",
          flag(kui_window_is_minimized));
    check("state.should_close", flag(kui_window_should_close) == 0, "should_close %d",
          flag(kui_window_should_close));
    status = kui_window_has_focus(g_window, &focused);
    check("state.has_focus", status == KUI_OK && (focused == 0 || focused == 1),
          "status %d focused %d", status, focused);

    check("state.is_maximized-initial", flag(kui_window_is_maximized) == 0, "is_maximized %d",
          flag(kui_window_is_maximized));
    status = kui_window_maximize(g_window);
    g = geometry();
    check("state.maximize", status == KUI_OK && flag(kui_window_is_maximized) == 1 &&
                                g.width >= g_orig.width,
          "status %d is_maximized %d size %dx%d", status, flag(kui_window_is_maximized), g.width,
          g.height);
    status = kui_window_restore(g_window);
    g = geometry();
    check("state.restore", status == KUI_OK && flag(kui_window_is_maximized) == 0 &&
                               g.width == g_orig.width && g.height == g_orig.height,
          "status %d is_maximized %d size %dx%d", status, flag(kui_window_is_maximized), g.width,
          g.height);

    check("state.is_fullscreen-initial", flag(kui_window_is_fullscreen) == 0, "is_fullscreen %d",
          flag(kui_window_is_fullscreen));
    status = kui_window_set_fullscreen(g_window, 1);
    g = geometry();
    check("state.set_fullscreen", status == KUI_OK && flag(kui_window_is_fullscreen) == 1 &&
                                      g.width >= g_orig.width,
          "status %d is_fullscreen %d size %dx%d", status, flag(kui_window_is_fullscreen), g.width,
          g.height);
    status = kui_window_set_fullscreen(g_window, 0);
    g = geometry();
    check("state.set_fullscreen-exit", status == KUI_OK && flag(kui_window_is_fullscreen) == 0 &&
                                           g.width == g_orig.width && g.height == g_orig.height &&
                                           g.x == g_orig.x && g.y == g_orig.y,
          "status %d is_fullscreen %d geometry %dx%d at (%d, %d)", status,
          flag(kui_window_is_fullscreen), g.width, g.height, g.x, g.y);
}

/* ---- Frame shape: radii, frameless, shadow ----------------------------------------------- */

typedef struct Radii {
    float tl, tr, br, bl;
} Radii;

static KuiStatus radii(Radii* r) {
    memset(r, 0, sizeof *r);
    return kui_window_get_frame_radii(g_window, &r->tl, &r->tr, &r->br, &r->bl);
}

/* ---- Host integration: one frame now, enable / disable, render DPR, base path ------------------ */

static void check_host(void) {
    KuiStatus status = kui_window_process_frame(g_window);
    KuiStatus read_status;
    int was_enabled = -1, disabled = -1, now_enabled = -1;
    float ratio = 0.0f;
    char original[512], changed[512], restored[512];

    check("host.process_frame", status == KUI_OK, "status %d", (int)status);

    /* Enabling / disabling input is Windows only; other platforms report NOT_SUPPORTED. */
    read_status = kui_window_is_enabled(g_window, &was_enabled);
    if (read_status == KUI_ERR_NOT_SUPPORTED) {
        status = kui_window_set_enabled(g_window, 0);
        check("host.set_enabled", status == KUI_ERR_NOT_SUPPORTED, "status %d", (int)status);
    } else {
        kui_window_set_enabled(g_window, 0);
        kui_window_is_enabled(g_window, &disabled);
        kui_window_set_enabled(g_window, 1);
        kui_window_is_enabled(g_window, &now_enabled);
        check("host.set_enabled", was_enabled == 1 && disabled == 0 && now_enabled == 1,
              "was %d, disabled %d, now %d", was_enabled, disabled, now_enabled);
    }

    /* Setting the render ratio to the value it already has leaves the page as it is. */
    kui_window_get_device_pixel_ratio(g_window, &ratio);
    status = kui_document_set_render_device_pixel_ratio(g_doc, ratio);
    check("host.set_render_device_pixel_ratio", status == KUI_OK, "status %d, ratio %.3f", (int)status, (double)ratio);

    /* A window created hidden (no flash), its first frame rendered while it is still hidden, then shown. */
    {
        KuiWindowPropsEx ex;
        KuiWindow* extra = NULL;
        int hidden = -1, shown = -1;
        memset(&ex, 0, sizeof ex);
        ex.struct_size = sizeof ex;
        ex.base.title = "kui-hidden";
        ex.base.width = 320;
        ex.base.height = 200;
        ex.base.frameless = 1;
        ex.sizeable = 1;
        ex.hidden = 1;
        ex.has_frame_radius = 1;
        ex.frame_radius = 8.0f;
        status = kui_window_create_ex(&ex, &extra);
        if (status == KUI_OK && extra) {
            kui_window_is_visible(extra, &hidden);
            kui_window_process_frame(extra);
            kui_window_show(extra);
            kui_window_is_visible(extra, &shown);
            kui_window_release(extra);
        }
        check("host.create_hidden_window", status == KUI_OK && hidden == 0 && shown == 1,
              "status %d, visible at creation %d, after show %d", (int)status, hidden, shown);
    }

    original[0] = changed[0] = restored[0] = '\0';
    kui_document_get_base_path(g_doc, original, sizeof original, NULL);
    kui_document_set_base_path(g_doc, "assets/", 0);
    kui_document_get_base_path(g_doc, changed, sizeof changed, NULL);
    kui_document_set_base_path(g_doc, original, 0);
    kui_document_get_base_path(g_doc, restored, sizeof restored, NULL);
    check("host.set_get_base_path", strcmp(changed, "assets/") == 0 && strcmp(restored, original) == 0,
          "changed '%s', restored '%s'", changed, restored);
}

static void check_frame(void) {
    Radii r;
    Geometry g;
    KuiStatus status;
    int frameless = -1, shadow = -1, sizeable = -1;

    status = radii(&r);
    check("frame.get_frame_radii-none", status == KUI_ERR_NOT_FOUND, "status %d", status);
    kui_window_set_frame_radius(g_window, 12.0f);
    status = radii(&r);
    check("frame.set_frame_radius", status == KUI_OK && near_f(r.tl, 12.0f) && near_f(r.tr, 12.0f) &&
                                        near_f(r.br, 12.0f) && near_f(r.bl, 12.0f),
          "status %d radii %.1f %.1f %.1f %.1f", status, r.tl, r.tr, r.br, r.bl);
    kui_window_set_frame_radii(g_window, 16.0f, 4.0f, 16.0f, 4.0f);
    status = radii(&r);
    check("frame.set_frame_radii", status == KUI_OK && near_f(r.tl, 16.0f) && near_f(r.tr, 4.0f) &&
                                       near_f(r.br, 16.0f) && near_f(r.bl, 4.0f),
          "status %d radii %.1f %.1f %.1f %.1f", status, r.tl, r.tr, r.br, r.bl);
    /* A negative radius is not a valid shape: the call changes nothing. */
    kui_window_set_frame_radii(g_window, -1.0f, 4.0f, 4.0f, 4.0f);
    status = radii(&r);
    check("frame.set_frame_radii-negative-ignored", status == KUI_OK && near_f(r.tl, 16.0f) &&
                                                        near_f(r.tr, 4.0f),
          "status %d radii %.1f %.1f %.1f %.1f", status, r.tl, r.tr, r.br, r.bl);
    status = kui_window_clear_frame_radius(g_window);
    check("frame.clear_frame_radius", status == KUI_OK && radii(&r) == KUI_ERR_NOT_FOUND,
          "status %d", status);

    status = kui_window_set_frameless(g_window, 1);
    kui_window_get_frame_state(g_window, &frameless, NULL, NULL);
    g = geometry();
    check("frame.set_frameless", status == KUI_OK && frameless == 1 && g.width == g_orig.width &&
                                     g.height == g_orig.height,
          "status %d frameless %d client %dx%d", status, frameless, g.width, g.height);
    status = kui_window_set_frameless_shadow(g_window, 1);
    kui_window_get_frame_state(g_window, NULL, &shadow, NULL);
    check("frame.set_frameless_shadow", status == KUI_OK && shadow == 1, "status %d shadow %d",
          status, shadow);
    kui_window_set_frameless_shadow(g_window, g_orig_shadow);
    kui_window_set_frameless(g_window, g_orig_frameless);
    kui_window_get_frame_state(g_window, &frameless, &shadow, &sizeable);
    g = geometry();
    check("frame.get_frame_state-restored", frameless == g_orig_frameless &&
                                                shadow == g_orig_shadow &&
                                                sizeable == g_orig_sizeable &&
                                                g.width == g_orig.width &&
                                                g.height == g_orig.height,
          "state %d/%d/%d (was %d/%d/%d) client %dx%d", frameless, shadow, sizeable,
          g_orig_frameless, g_orig_shadow, g_orig_sizeable, g.width, g.height);
}

/* ---- Opacity, z-order, cursor, IME candidate position ----------------------------------- */

static void ime_caret(float* x, float* y) {
    KuiElement* input = kuex_by_id(g_doc, "ime-input");
    KuiRect rect;
    memset(&rect, 0, sizeof rect);
    if (input) {
        kui_element_get_bounding_rect(input, &rect);
        kui_element_release(input);
    }
    *x = rect.x + 4.0f;
    *y = rect.y + rect.height;
}

static void check_look(void) {
    KuiStatus status, low, high;
    float x, y;

    status = kui_window_set_opacity(g_window, 0.85f);
    low = kui_window_set_opacity(g_window, -0.1f);
    high = kui_window_set_opacity(g_window, 1.5f);
    check("look.set_opacity", status == KUI_OK && low == KUI_ERR_INVALID_ARG &&
                                  high == KUI_ERR_INVALID_ARG &&
                                  kui_window_set_opacity(g_window, 1.0f) == KUI_OK,
          "0.85 -> %d, -0.1 -> %d, 1.5 -> %d", status, low, high);
    status = kui_window_set_always_on_top(g_window, 1);
    check("look.set_always_on_top", status == KUI_OK &&
                                        kui_window_set_always_on_top(g_window, 0) == KUI_OK,
          "status %d", status);
    status = kui_window_set_cursor(g_window, KUI_C_CURSOR_CROSSHAIR);
    low = kui_window_set_cursor(g_window, (KuiCCursor)99);
    check("look.set_cursor", status == KUI_OK && low == KUI_ERR_INVALID_ARG &&
                                 kui_window_set_cursor(g_window, KUI_C_CURSOR_DEFAULT) == KUI_OK,
          "crosshair -> %d, 99 -> %d", status, low);
    status = kui_window_set_cursor_visible(g_window, 0);
    check("look.set_cursor_visible", status == KUI_OK &&
                                         kui_window_set_cursor_visible(g_window, 1) == KUI_OK,
          "status %d", status);
    ime_caret(&x, &y);
    status = kui_window_set_ime_candidate_pos(g_window, x, y);
    check("look.set_ime_candidate_pos", status == KUI_OK, "status %d at (%.0f, %.0f)", status, x, y);
}

/* ---- Native menu bar ----------------------------------------------------------------------- */

enum { MENU_WRAP = 901, MENU_RELOAD = 902, MENU_ZOOM_IN = 903, MENU_ZOOM_OUT = 904 };
static int g_menu_checked = 1;
static int g_menu_enabled = 1;

static void on_menu(uint32_t item_id, void* user_data) {
    char text[64];
    (void)user_data;
    snprintf(text, sizeof text, "menu item %u activated", (unsigned)item_id);
    kuex_set_text(g_doc, "menu-out", text);
}

static void item(KuiCMenuItem* out, const char* text, uint32_t id) {
    memset(out, 0, sizeof *out);
    out->text = text;
    out->id = id;
}

static KuiStatus install_menu(void) {
    KuiCMenuItem zoom[2];
    KuiCMenuItem view[4];
    KuiCMenu menu;
    item(&zoom[0], "Zoom &in", MENU_ZOOM_IN);
    zoom[0].shortcut_label = "Ctrl++";
    item(&zoom[1], "Zoom &out", MENU_ZOOM_OUT);
    item(&view[0], "&Word wrap", MENU_WRAP);
    view[0].checkable = 1;
    view[0].checked = 1;
    item(&view[1], NULL, 0);
    view[1].separator = 1;
    item(&view[2], "&Reload", MENU_RELOAD);
    item(&view[3], "&Zoom", 0);
    view[3].children = zoom;
    view[3].child_count = 2;
    memset(&menu, 0, sizeof menu);
    menu.text = "&View";
    menu.items = view;
    menu.item_count = 4;
    g_menu_checked = 1;
    g_menu_enabled = 1;
    return kui_window_set_menu_bar(g_window, &menu, 1, on_menu, NULL);
}

static void check_menu(void) {
    Geometry g;
    KuiStatus status, nested, missing;

    status = kui_window_menu_set_checked(g_window, MENU_WRAP, 0);
    check("menu.set_checked-no-menu", status == KUI_ERR_NOT_FOUND, "status %d", status);
    status = install_menu();
    g = geometry();
    check("menu.set_menu_bar", status == KUI_OK, "status %d", status);
#ifdef _WIN32
    /* A native menu bar takes its height from the client area. */
    check("menu.set_menu_bar-client-area", g.width == g_orig.width && g.height < g_orig.height,
          "client %dx%d, was %dx%d", g.width, g.height, g_orig.width, g_orig.height);
#else
    (void)g;
    skip("menu.set_menu_bar-client-area", "the menu bar only takes client height on Windows");
#endif
    status = kui_window_menu_set_checked(g_window, MENU_WRAP, 0);
    nested = kui_window_menu_set_checked(g_window, MENU_ZOOM_OUT, 1);
    missing = kui_window_menu_set_checked(g_window, 999, 1);
    check("menu.menu_set_checked", status == KUI_OK && nested == KUI_OK &&
                                       missing == KUI_ERR_NOT_FOUND,
          "top-level %d, submenu %d, unknown id %d", status, nested, missing);
    status = kui_window_menu_set_enabled(g_window, MENU_RELOAD, 0);
    missing = kui_window_menu_set_enabled(g_window, 999, 0);
    check("menu.menu_set_enabled", status == KUI_OK && missing == KUI_ERR_NOT_FOUND &&
                                       kui_window_menu_set_enabled(g_window, MENU_RELOAD, 1) == KUI_OK,
          "status %d, unknown id %d", status, missing);
    status = kui_window_clear_menu_bar(g_window);
    missing = kui_window_menu_set_checked(g_window, MENU_WRAP, 1);
    g = geometry();
    check("menu.clear_menu_bar", status == KUI_OK && missing == KUI_ERR_NOT_FOUND &&
                                     g.width == g_orig.width && g.height == g_orig.height,
          "status %d, set_checked afterwards %d, client %dx%d", status, missing, g.width, g.height);
}

/* ---- Keyboard shortcuts --------------------------------------------------------------------- */

static int g_f7_first;   /* the callback F7 was bound to first (replaced before any press) */
static int g_f7_hits;    /* the replacing callback */
static int g_ctrl_k_hits;
static int g_button_hits;
static int g_button_bound;

static void on_f7_first(void* user_data) {
    (void)user_data;
    g_f7_first++;
}

static void on_f7(void* user_data) {
    (void)user_data;
    g_f7_hits++;
}

static void on_ctrl_k(void* user_data) {
    (void)user_data;
    g_ctrl_k_hits++;
}

static void check_shortcuts(void) {
    uint32_t f7 = 0, f7_again = 0, ctrl_k = 0, bad = 0;
    KuiStatus status, rebind, invalid, empty;
    int hits;

    status = kui_window_bind_shortcut(g_window, "F7", 0, on_f7_first, NULL, &f7);
    kui_window_bind_shortcut(g_window, "Ctrl+Shift+K", 0, on_ctrl_k, NULL, &ctrl_k);
    check("sc.bind_shortcut", status == KUI_OK && f7 != 0 && ctrl_k != 0 && ctrl_k != f7,
          "status %d ids %u / %u", status, (unsigned)f7, (unsigned)ctrl_k);
    rebind = kui_window_bind_shortcut(g_window, "F7", 0, on_f7, NULL, &f7_again);
    check("sc.bind_shortcut-rebind-same-id", rebind == KUI_OK && f7_again == f7,
          "status %d id %u, first id %u", rebind, (unsigned)f7_again, (unsigned)f7);
    invalid = kui_window_bind_shortcut(g_window, "Ctrl+", 0, on_f7, NULL, &bad);
    empty = kui_window_bind_shortcut(g_window, "", 0, on_f7, NULL, &bad);
    check("sc.bind_shortcut-invalid", invalid == KUI_ERR_INVALID_ARG && empty == KUI_ERR_INVALID_ARG,
          "'Ctrl+' -> %d, '' -> %d", invalid, empty);

    /* Synthetic DevTools input goes to the page's event dispatcher. */
    kui_devtools_dispatch_key_event(g_doc, KUI_C_KEY_DOWN, "F7", 0, "F7", 0, 0, NULL, 0, 0);
    kui_devtools_dispatch_key_event(g_doc, KUI_C_KEY_UP, "F7", 0, "F7", 0, 0, NULL, 0, 0);
    known("sc.devtools-key-reaches-shortcut", g_f7_hits == 1,
          "devtools_dispatch_key_event bypasses the window ShortcutManager (only the native "
          "key-down path calls tryDispatch), so injected keys never trigger bound shortcuts",
          "F7 callback ran %d time(s)", g_f7_hits);

    hits = g_f7_hits;
    if (send_native_key(KEY_F7)) {
        check("sc.native-key-fires", g_f7_hits == hits + 1 && g_f7_first == 0,
              "replacing callback %d -> %d, replaced callback %d", hits, g_f7_hits, g_f7_first);
        hits = g_f7_hits;
        status = kui_window_set_shortcuts_enabled(g_window, 0);
        send_native_key(KEY_F7);
        check("sc.set_shortcuts_enabled-off", status == KUI_OK && g_f7_hits == hits,
              "status %d hits %d -> %d", status, hits, g_f7_hits);
        status = kui_window_set_shortcuts_enabled(g_window, 1);
        send_native_key(KEY_F7);
        check("sc.set_shortcuts_enabled-on", status == KUI_OK && g_f7_hits == hits + 1,
              "status %d hits %d -> %d", status, hits, g_f7_hits);
        hits = g_f7_hits;
        status = kui_window_unbind_shortcut(g_window, f7);
        send_native_key(KEY_F7);
        check("sc.unbind_shortcut", status == KUI_OK && g_f7_hits == hits, "status %d hits %d -> %d",
              status, hits, g_f7_hits);
        kui_window_bind_shortcut(g_window, "F7", 0, on_f7, NULL, &f7);
        status = kui_window_unbind_all_shortcuts(g_window);
        send_native_key(KEY_F7);
        check("sc.unbind_all_shortcuts", status == KUI_OK && g_f7_hits == hits,
              "status %d hits %d -> %d", status, hits, g_f7_hits);
    } else {
        skip("sc.native-key-fires", "no native key injection on this platform");
        status = kui_window_set_shortcuts_enabled(g_window, 0);
        check("sc.set_shortcuts_enabled", status == KUI_OK &&
                                              kui_window_set_shortcuts_enabled(g_window, 1) == KUI_OK,
              "status %d", status);
        status = kui_window_unbind_shortcut(g_window, f7);
        check("sc.unbind_shortcut", status == KUI_OK, "status %d", status);
        status = kui_window_unbind_all_shortcuts(g_window);
        check("sc.unbind_all_shortcuts", status == KUI_OK, "status %d", status);
    }
    check("sc.ctrl-shift-k-untouched", g_ctrl_k_hits == 0, "Ctrl+Shift+K ran %d time(s)",
          g_ctrl_k_hits);
}

/* ---- Tray icon (created hidden; simulated clicks) -------------------------------------- */

static int g_tray_events[8];
static int g_tray_count;
static const char* k_tray_names[] = {"click", "double click", "right click", "balloon click"};

static void on_tray(KuiCTrayEvent event, void* user_data) {
    char text[96];
    (void)user_data;
    if (g_tray_count < 8) g_tray_events[g_tray_count] = (int)event;
    g_tray_count++;
    snprintf(text, sizeof text, "tray callback: %s (events=%d)",
             (event >= 0 && event <= 3) ? k_tray_names[event] : "?", g_tray_count);
    kuex_set_text(g_doc, "tray-out", text);
}

/* A hidden tray icon with the application icon; 0 when the platform has no tray. */
static int ensure_tray(int visible) {
    int active = 0;
    kui_window_tray_is_active(g_window, &active);
    if (!active && kui_window_tray_create(g_window, NULL, 0, "KUI API tour", 0, visible) != KUI_OK) {
        return 0;
    }
    if (visible) kui_window_tray_set_visible(g_window, 1);
    kui_window_tray_set_event_callback(g_window, on_tray, NULL);
    return 1;
}

static void check_tray(void) {
    int active = -1, visible = -1;
    KuiStatus status;

    kui_window_tray_is_active(g_window, &active);
    kui_window_tray_is_visible(g_window, &visible);
    check("tray.is_active-before-create", active == 0 && visible == 0, "active %d visible %d",
          active, visible);
    if (!ensure_tray(0)) {
        skip("tray.simulate_event", "this platform has no tray icon");
        return;
    }
    kui_window_tray_is_active(g_window, &active);
    kui_window_tray_is_visible(g_window, &visible);
    check("tray.tray_is_active-hidden", active == 1 && visible == 0, "active %d visible %d", active,
          visible);
    g_tray_count = 0;
    kui_window_tray_simulate_event(g_window, KUI_C_TRAY_CLICK);
    kui_window_tray_simulate_event(g_window, KUI_C_TRAY_DOUBLE_CLICK);
    kui_window_tray_simulate_event(g_window, KUI_C_TRAY_RIGHT_CLICK);
    status = kui_window_tray_simulate_event(g_window, KUI_C_TRAY_BALLOON_CLICK);
    check("tray.tray_simulate_event", status == KUI_OK && g_tray_count == 4 &&
                                          g_tray_events[0] == KUI_C_TRAY_CLICK &&
                                          g_tray_events[1] == KUI_C_TRAY_DOUBLE_CLICK &&
                                          g_tray_events[2] == KUI_C_TRAY_RIGHT_CLICK &&
                                          g_tray_events[3] == KUI_C_TRAY_BALLOON_CLICK,
          "status %d, %d event(s), first %d", status, g_tray_count, g_tray_events[0]);
    status = kui_window_tray_set_icon(g_window, "", 0);
    kui_window_tray_is_active(g_window, &active);
    check("tray.tray_set_icon", status == KUI_OK && active == 1, "status %d active %d", status,
          active);
    kui_window_tray_destroy(g_window);
    kui_window_tray_is_active(g_window, &active);
    check("tray.destroy", active == 0, "active %d", active);
}

/* ---- DevTools state and screenshot ------------------------------------------------------- */

static void temp_path(const char* name, char* buf, size_t capacity) {
#ifdef _WIN32
    char dir[MAX_PATH + 1];
    DWORD length = GetTempPathA((DWORD)sizeof dir, dir);   /* ends with a backslash */
    if (length == 0 || length >= sizeof dir) snprintf(dir, sizeof dir, ".\\");
    snprintf(buf, capacity, "%s%s", dir, name);
#else
    const char* dir = getenv("TMPDIR");
    if (!dir || !*dir) dir = "/tmp";
    snprintf(buf, capacity, "%s/%s", dir, name);
#endif
}

/* Size of a PNG file, 0 when missing or not a PNG. */
static long png_size(const char* path) {
    unsigned char head[8];
    long size = 0;
    FILE* file = fopen(path, "rb");
    if (!file) return 0;
    if (fread(head, 1, sizeof head, file) == sizeof head && memcmp(head, "\x89PNG\r\n\x1a\n", 8) == 0 &&
        fseek(file, 0, SEEK_END) == 0) {
        size = ftell(file);
    }
    fclose(file);
    return size;
}

static void check_devtools_screenshot(void) {
    char path[512];
    uint16_t port = 0, state_port = 0;
    uint32_t clients = 0;
    int running = -1, saved = -1;
    long size;
    KuiStatus status;

    kui_window_get_devtools_port(g_window, &port);
    status = kui_window_get_devtools_state(g_window, &running, &state_port, &clients);
    check("dt.get_devtools_state", status == KUI_OK && running == 1 && port != 0 &&
                                       state_port == port,
          "status %d running %d port %u (get_devtools_port %u) clients %u", status, running,
          (unsigned)state_port, (unsigned)port, (unsigned)clients);

    temp_path("kui_api_window_shot.png", path, sizeof path);
    remove(path);
    status = kui_window_save_screenshot(g_window, path, 0, &saved);
    size = png_size(path);
    check("dt.save_screenshot", status == KUI_OK && saved == 1 && size > 100,
          "status %d saved %d, '%s' holds %ld PNG bytes", status, saved, path, size);
    remove(path);
    temp_path("kui-api-window-no-such-dir/shot.png", path, sizeof path);
    saved = -1;
    status = kui_window_save_screenshot(g_window, path, 0, &saved);
    check("dt.save_screenshot-unwritable", status == KUI_OK && saved == 0, "status %d saved %d",
          status, saved);
}

/* ---- Everything back as it was --------------------------------------------------------- */

static void check_restored(void) {
    char title[256];
    Radii r;
    Geometry g = geometry();
    int frameless = -1, shadow = -1, sizeable = -1;
    window_title(title, sizeof title);
    kui_window_get_frame_state(g_window, &frameless, &shadow, &sizeable);
    check("win.state-restored", strcmp(title, g_orig_title) == 0 && g.width == g_orig.width &&
                                    g.height == g_orig.height && g.x == g_orig.x &&
                                    g.y == g_orig.y && flag(kui_window_is_maximized) == 0 &&
                                    flag(kui_window_is_fullscreen) == 0 &&
                                    frameless == g_orig_frameless && shadow == g_orig_shadow &&
                                    radii(&r) == KUI_ERR_NOT_FOUND,
          "title '%s' geometry %dx%d at (%d, %d), was %dx%d at (%d, %d)", title, g.width, g.height,
          g.x, g.y, g_orig.width, g_orig.height, g_orig.x, g_orig.y);
}

/* ---- Panel buttons ----------------------------------------------------------------------- */

static void show_geometry(void) {
    char title[256];
    char text[400];
    float ratio = 0.0f;
    void* handle = NULL;
    Geometry g = geometry();
    window_title(title, sizeof title);
    kui_window_get_device_pixel_ratio(g_window, &ratio);
    kui_window_get_native_handle(g_window, &handle);
    snprintf(text, sizeof text, "size=%dx%d pos=(%d, %d) dpr=%.2f handle=%s title=%s", g.width,
             g.height, g.x, g.y, ratio, handle ? "yes" : "no", title);
    kuex_set_text(g_doc, "geom-out", text);
}

static void on_geom_read(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    show_geometry();
}

static void on_geom_shrink(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_window_resize(g_window, g_orig.width - 60, g_orig.height - 40);
    show_geometry();
}

static void on_geom_restore(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_window_resize(g_window, g_orig.width, g_orig.height);
    show_geometry();
}

static int g_title_toggle;

static void on_title(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    g_title_toggle = !g_title_toggle;
    kui_window_set_title(g_window, g_title_toggle ? "Title set from a button" : g_orig_title, 0);
    show_geometry();
}

static void show_state(const char* prefix) {
    char text[200];
    int focused = 0;
    kui_window_has_focus(g_window, &focused);
    snprintf(text, sizeof text, "%smaximized=%d minimized=%d fullscreen=%d focused=%d should_close=%d",
             prefix, flag(kui_window_is_maximized), flag(kui_window_is_minimized),
             flag(kui_window_is_fullscreen), focused, flag(kui_window_should_close));
    kuex_set_text(g_doc, "state-out", text);
}

static void on_state(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    show_state("");
}

static void on_max_cycle(KuiEvent* e, void* u) {
    char prefix[64];
    (void)e;
    (void)u;
    kui_window_maximize(g_window);
    snprintf(prefix, sizeof prefix, "while maximized: %d | after restore: ",
             flag(kui_window_is_maximized));
    kui_window_restore(g_window);
    show_state(prefix);
}

static void on_fs_cycle(KuiEvent* e, void* u) {
    char prefix[64];
    (void)e;
    (void)u;
    kui_window_set_fullscreen(g_window, 1);
    snprintf(prefix, sizeof prefix, "while fullscreen: %d | after exit: ",
             flag(kui_window_is_fullscreen));
    kui_window_set_fullscreen(g_window, 0);
    show_state(prefix);
}

/* Manual: a minimized window stops rendering until it is restored from the taskbar. */
static void on_minimize(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_window_minimize(g_window);
}

static void show_frame(void) {
    char text[200];
    Radii r;
    int frameless = 0, shadow = 0, sizeable = 0;
    KuiStatus status = radii(&r);
    kui_window_get_frame_state(g_window, &frameless, &shadow, &sizeable);
    if (status == KUI_OK) {
        snprintf(text, sizeof text, "radii=%.0f/%.0f/%.0f/%.0f frameless=%d shadow=%d sizeable=%d",
                 r.tl, r.tr, r.br, r.bl, frameless, shadow, sizeable);
    } else {
        snprintf(text, sizeof text, "radii=none frameless=%d shadow=%d sizeable=%d", frameless,
                 shadow, sizeable);
    }
    kuex_set_text(g_doc, "frame-out", text);
}

static void on_radius(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_window_set_frame_radius(g_window, 12.0f);
    show_frame();
}

static void on_radii(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_window_set_frame_radii(g_window, 16.0f, 4.0f, 16.0f, 4.0f);
    show_frame();
}

static void on_radius_clear(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_window_clear_frame_radius(g_window);
    show_frame();
}

static void on_frameless(KuiEvent* e, void* u) {
    int frameless = 0;
    (void)e;
    (void)u;
    kui_window_get_frame_state(g_window, &frameless, NULL, NULL);
    kui_window_set_frameless(g_window, !frameless);
    kui_window_set_frameless_shadow(g_window, !frameless);
    show_frame();
}

static void on_opacity(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kuex_set_text(g_doc, "look-out", kui_window_set_opacity(g_window, 0.85f) == KUI_OK
                                         ? "opacity 0.85" : "set_opacity failed");
}

static void on_opaque(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kuex_set_text(g_doc, "look-out", kui_window_set_opacity(g_window, 1.0f) == KUI_OK
                                         ? "opacity 1.00" : "set_opacity failed");
}

static void on_top(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kuex_set_text(g_doc, "look-out", kui_window_set_always_on_top(g_window, 1) == KUI_OK
                                         ? "always on top" : "set_always_on_top failed");
}

static void on_untop(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kuex_set_text(g_doc, "look-out", kui_window_set_always_on_top(g_window, 0) == KUI_OK
                                         ? "normal z-order" : "set_always_on_top failed");
}

static void on_cursor(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kuex_set_text(g_doc, "look-out", kui_window_set_cursor(g_window, KUI_C_CURSOR_WAIT) == KUI_OK
                                         ? "cursor: wait (until the pointer moves to other content)"
                                         : "set_cursor failed");
}

static void show_cursor_again(void* user_data) {
    (void)user_data;
    kui_window_set_cursor_visible(g_window, 1);
    kuex_set_text(g_doc, "look-out", "cursor visible again");
}

static void on_cursor_hide(KuiEvent* e, void* u) {
    KuiTimerId timer = 0;
    (void)e;
    (void)u;
    kui_window_set_cursor_visible(g_window, 0);
    kuex_set_text(g_doc, "look-out", "cursor hidden for 1 s");
    kui_platform_set_timeout(show_cursor_again, NULL, 1000, &timer);
}

static void on_ime(KuiEvent* e, void* u) {
    char text[96];
    float x, y;
    KuiElement* input = kuex_by_id(g_doc, "ime-input");
    (void)e;
    (void)u;
    if (input) {
        kui_element_focus(input);
        kui_element_release(input);
    }
    ime_caret(&x, &y);
    snprintf(text, sizeof text, "IME candidates at (%.0f, %.0f): status %d", x, y,
             kui_window_set_ime_candidate_pos(g_window, x, y));
    kuex_set_text(g_doc, "look-out", text);
}

static void on_menu_set(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kuex_set_text(g_doc, "menu-out", install_menu() == KUI_OK
                                         ? "menu installed: View > Word wrap (checked), Reload, Zoom"
                                         : "set_menu_bar failed");
}

static void on_menu_check(KuiEvent* e, void* u) {
    char text[64];
    KuiStatus status;
    (void)e;
    (void)u;
    status = kui_window_menu_set_checked(g_window, MENU_WRAP, !g_menu_checked);
    if (status == KUI_OK) g_menu_checked = !g_menu_checked;
    snprintf(text, sizeof text, "Word wrap checked=%d (status %d)", g_menu_checked, status);
    kuex_set_text(g_doc, "menu-out", text);
}

static void on_menu_enable(KuiEvent* e, void* u) {
    char text[64];
    KuiStatus status;
    (void)e;
    (void)u;
    status = kui_window_menu_set_enabled(g_window, MENU_RELOAD, !g_menu_enabled);
    if (status == KUI_OK) g_menu_enabled = !g_menu_enabled;
    snprintf(text, sizeof text, "Reload enabled=%d (status %d)", g_menu_enabled, status);
    kuex_set_text(g_doc, "menu-out", text);
}

static void on_menu_clear(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kuex_set_text(g_doc, "menu-out", kui_window_clear_menu_bar(g_window) == KUI_OK
                                         ? "menu removed" : "clear_menu_bar failed");
}

static void on_button_f7(void* user_data) {
    char text[64];
    (void)user_data;
    snprintf(text, sizeof text, "F7 fired %d time(s)", ++g_button_hits);
    kuex_set_text(g_doc, "sc-out", text);
}

static void on_sc_bind(KuiEvent* e, void* u) {
    uint32_t id = 0;
    char text[64];
    (void)e;
    (void)u;
    g_button_hits = 0;
    g_button_bound = kui_window_bind_shortcut(g_window, "F7", 0, on_button_f7, NULL, &id) == KUI_OK;
    snprintf(text, sizeof text, g_button_bound ? "F7 bound (id %u)" : "bind failed", (unsigned)id);
    kuex_set_text(g_doc, "sc-out", text);
}

static void on_sc_press(KuiEvent* e, void* u) {
    char text[64];
    int before = g_button_hits;
    (void)e;
    (void)u;
    if (!send_native_key(KEY_F7)) {
        kuex_set_text(g_doc, "sc-out", "no native key injection on this platform: press F7");
        return;
    }
    if (g_button_hits == before) {
        snprintf(text, sizeof text, "F7 sent, no shortcut ran (fired %d time(s))", g_button_hits);
        kuex_set_text(g_doc, "sc-out", text);
    }
}

static void on_sc_disable(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_window_set_shortcuts_enabled(g_window, 0);
    kuex_set_text(g_doc, "sc-out", "shortcuts disabled");
}

static void on_sc_enable(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_window_set_shortcuts_enabled(g_window, 1);
    kuex_set_text(g_doc, "sc-out", "shortcuts enabled");
}

static void on_sc_unbind(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_window_unbind_all_shortcuts(g_window);
    g_button_bound = 0;
    kuex_set_text(g_doc, "sc-out", "all shortcuts unbound");
}

static void on_tray_click(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    if (!ensure_tray(0)) {
        kuex_set_text(g_doc, "tray-out", "no tray on this platform");
        return;
    }
    kui_window_tray_simulate_event(g_window, KUI_C_TRAY_CLICK);
}

static void on_tray_right(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    if (!ensure_tray(0)) {
        kuex_set_text(g_doc, "tray-out", "no tray on this platform");
        return;
    }
    kui_window_tray_simulate_event(g_window, KUI_C_TRAY_RIGHT_CLICK);
}

static void on_tray_menu(uint32_t item_id, void* user_data) {
    char text[64];
    (void)user_data;
    snprintf(text, sizeof text, "tray menu item %u", (unsigned)item_id);
    kuex_set_text(g_doc, "tray-out", text);
}

/* Manual: shows the tray icon and opens its context menu (a system popup). */
static void on_tray_popup(KuiEvent* e, void* u) {
    KuiCMenuItem items[2];
    (void)e;
    (void)u;
    if (!ensure_tray(1)) {
        kuex_set_text(g_doc, "tray-out", "no tray on this platform");
        return;
    }
    item(&items[0], "Say hello", 1);
    item(&items[1], "Hide icon", 2);
    kui_window_tray_set_context_menu(g_window, items, 2, on_tray_menu, NULL);
    kuex_set_text(g_doc, "tray-out", kui_window_tray_popup_context_menu(g_window) == KUI_OK
                                         ? "context menu shown" : "no context menu on this platform");
}

static void on_shot(KuiEvent* e, void* u) {
    char path[512];
    char text[600];
    int saved = 0;
    long size;
    (void)e;
    (void)u;
    temp_path("kui_api_window_button.png", path, sizeof path);
    kui_window_save_screenshot(g_window, path, 0, &saved);
    size = png_size(path);
    snprintf(text, sizeof text, "saved=%d PNG bytes=%s (%s, removed again)", saved,
             size > 100 ? ">100" : "0", path);
    remove(path);
    kuex_set_text(g_doc, "dt-out", text);
}

static void on_dt_state(KuiEvent* e, void* u) {
    char text[96];
    int running = 0;
    uint16_t port = 0;
    uint32_t clients = 0;
    (void)e;
    (void)u;
    kui_window_get_devtools_state(g_window, &running, &port, &clients);
    snprintf(text, sizeof text, "devtools running=%d port=%u clients=%u", running, (unsigned)port,
             (unsigned)clients);
    kuex_set_text(g_doc, "dt-out", text);
}

/* Manual: stopping the server disconnects every inspector (and the automated check). */
static void on_dt_stop(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_window_stop_devtools(g_window);
    kuex_set_text(g_doc, "dt-out", "devtools stopped");
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    int failed = 0;
    g_doc = host->document;
    g_window = host->window;
    g_script = host->script;

    window_title(g_orig_title, sizeof g_orig_title);
    g_orig = geometry();
    kui_window_get_frame_state(g_window, &g_orig_frameless, &g_orig_shadow, &g_orig_sizeable);

    check_geometry();
    check_state();
    check_frame();
    check_look();
    check_menu();
    check_shortcuts();
    check_tray();
    check_devtools_screenshot();
    check_host();
    check_restored();
    finish();

    failed |= kuex_on(g_doc, "btn-geom-read", "click", on_geom_read, NULL);
    failed |= kuex_on(g_doc, "btn-geom-shrink", "click", on_geom_shrink, NULL);
    failed |= kuex_on(g_doc, "btn-geom-restore", "click", on_geom_restore, NULL);
    failed |= kuex_on(g_doc, "btn-title", "click", on_title, NULL);
    failed |= kuex_on(g_doc, "btn-state", "click", on_state, NULL);
    failed |= kuex_on(g_doc, "btn-max-cycle", "click", on_max_cycle, NULL);
    failed |= kuex_on(g_doc, "btn-fs-cycle", "click", on_fs_cycle, NULL);
    failed |= kuex_on(g_doc, "btn-minimize", "click", on_minimize, NULL);
    failed |= kuex_on(g_doc, "btn-radius", "click", on_radius, NULL);
    failed |= kuex_on(g_doc, "btn-radii", "click", on_radii, NULL);
    failed |= kuex_on(g_doc, "btn-radius-clear", "click", on_radius_clear, NULL);
    failed |= kuex_on(g_doc, "btn-frameless", "click", on_frameless, NULL);
    failed |= kuex_on(g_doc, "btn-opacity", "click", on_opacity, NULL);
    failed |= kuex_on(g_doc, "btn-opaque", "click", on_opaque, NULL);
    failed |= kuex_on(g_doc, "btn-top", "click", on_top, NULL);
    failed |= kuex_on(g_doc, "btn-untop", "click", on_untop, NULL);
    failed |= kuex_on(g_doc, "btn-cursor", "click", on_cursor, NULL);
    failed |= kuex_on(g_doc, "btn-cursor-hide", "click", on_cursor_hide, NULL);
    failed |= kuex_on(g_doc, "btn-ime", "click", on_ime, NULL);
    failed |= kuex_on(g_doc, "btn-menu-set", "click", on_menu_set, NULL);
    failed |= kuex_on(g_doc, "btn-menu-check", "click", on_menu_check, NULL);
    failed |= kuex_on(g_doc, "btn-menu-enable", "click", on_menu_enable, NULL);
    failed |= kuex_on(g_doc, "btn-menu-clear", "click", on_menu_clear, NULL);
    failed |= kuex_on(g_doc, "btn-sc-bind", "click", on_sc_bind, NULL);
    failed |= kuex_on(g_doc, "btn-sc-press", "click", on_sc_press, NULL);
    failed |= kuex_on(g_doc, "btn-sc-disable", "click", on_sc_disable, NULL);
    failed |= kuex_on(g_doc, "btn-sc-enable", "click", on_sc_enable, NULL);
    failed |= kuex_on(g_doc, "btn-sc-unbind", "click", on_sc_unbind, NULL);
    failed |= kuex_on(g_doc, "btn-tray-click", "click", on_tray_click, NULL);
    failed |= kuex_on(g_doc, "btn-tray-right", "click", on_tray_right, NULL);
    failed |= kuex_on(g_doc, "btn-tray-popup", "click", on_tray_popup, NULL);
    failed |= kuex_on(g_doc, "btn-shot", "click", on_shot, NULL);
    failed |= kuex_on(g_doc, "btn-dt-state", "click", on_dt_state, NULL);
    failed |= kuex_on(g_doc, "btn-dt-stop", "click", on_dt_stop, NULL);
    return failed;
}
