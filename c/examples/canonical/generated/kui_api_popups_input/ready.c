/* Copied by scripts/generate_c_examples.py from examples/c/ports/kui_api_popups_input.c; edit that file, not this copy. */
/* API tour: popups, toasts and input pickers (examples/api_tours/popups_input). At start-up the
 * host shows and dismisses toasts, tooltips and context menus, reads the popup layer's state
 * back after each step, configures the tooltip theme / delay and the default context menu, and
 * drives the <input> picker panel (open, page, time, colour, commit, close, enable) checking the
 * picker state and the input's value (read with the script engine). #tour-result ends as
 * "PASS n/n"; the panel buttons repeat the calls with a visible effect. */
#include "kui_c_example_dom.h"

#include <math.h>
#include <stdarg.h>

static KuiDocument* g_doc;

/* ---- Self-check bookkeeping: count, log one <tr> per check, publish the verdict ---------- */

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

/* A documented behaviour the engine currently gets wrong: logged, not counted. */
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

/* ---- Small helpers ---------------------------------------------------------------------- */

static int near_f(double a, double b, double tolerance) { return fabs(a - b) <= tolerance; }

static int rect_of(const char* id, KuiRect* rect) {
    KuiElement* element = kuex_by_id(g_doc, id);
    int ok = element && kui_element_get_bounding_rect(element, rect) == KUI_OK;
    if (element) kui_element_release(element);
    if (!ok) memset(rect, 0, sizeof *rect);
    return ok;
}

static int has_type(KuiCPopupType type) {
    int has = -1;
    kui_document_has_popup_of_type(g_doc, type, &has);
    return has;
}

static int any_popup(void) {
    int any = -1;
    kui_document_has_popups(g_doc, &any);
    return any;
}

static int toast_count(void) {
    int count = -1;
    kui_document_get_toast_count(g_doc, &count);
    return count;
}

/* The input's current value (what the user typed), through kui_element_get_value. */
static void input_value(const char* id, char* buf, size_t capacity) {
    kuex_get_value(g_doc, id, buf, capacity);
}

static const char* status_name(KuiStatus status) {
    switch (status) {
        case KUI_OK: return "OK";
        case KUI_ERR_INVALID_ARG: return "INVALID_ARG";
        case KUI_ERR_NOT_FOUND: return "NOT_FOUND";
        case KUI_ERR_NOT_SUPPORTED: return "NOT_SUPPORTED";
        default: return "ERROR";
    }
}

static const char* mode_name(KuiCPickerMode mode) {
    switch (mode) {
        case KUI_C_PICKER_DATE: return "date";
        case KUI_C_PICKER_TIME: return "time";
        case KUI_C_PICKER_DATETIME_LOCAL: return "datetime-local";
        case KUI_C_PICKER_COLOR: return "color";
        default: return "none";
    }
}

/* ---- Toasts: show two, dismiss one by id, then all; the count is read back -------------- */

static uint32_t g_toast_ids[2];
static int g_toast_base;

static uint32_t show_toast(const char* message, KuiCToastLevel level, int duration_ms) {
    KuiCToastOptions options;
    uint32_t id = 0;
    memset(&options, 0, sizeof options);
    options.message = message;
    options.title = "KUI API tour";
    options.level = level;
    options.duration_ms = duration_ms;
    options.closable = 1;
    kui_document_show_toast(g_doc, &options, &id);
    return id;
}

static void check_toasts(void) {
    int after_show;
    KuiStatus status;
    g_toast_base = toast_count();
    g_toast_ids[0] = show_toast("First toast (stays until dismissed)", KUI_C_TOAST_INFO, 0);
    g_toast_ids[1] = show_toast("Second toast", KUI_C_TOAST_SUCCESS, 0);
    after_show = toast_count();
    check("toast.get_toast_count", g_toast_base >= 0 && after_show == g_toast_base + 2 &&
              g_toast_ids[0] && g_toast_ids[1] && g_toast_ids[0] != g_toast_ids[1],
          "count %d -> %d, ids %u %u", g_toast_base, after_show, g_toast_ids[0], g_toast_ids[1]);
    status = kui_document_dismiss_toast(g_doc, g_toast_ids[0]);
    check("toast.dismiss_toast", status == KUI_OK, "status %d", status);
    status = kui_document_dismiss_all_toasts(g_doc);
    check("toast.dismiss_all_toasts", status == KUI_OK, "status %d", status);
    /* Dismissed toasts play an exit animation and still count; the final phase checks 0. */
}

/* ---- Tooltip: delay and theme read back, a tooltip shown at an element's rect ----------- */

static int themes_equal(const KuiCTooltipTheme* a, const KuiCTooltipTheme* b) {
    return a->background == b->background && a->foreground == b->foreground &&
           a->border == b->border && near_f(a->radius, b->radius, 0.01);
}

static const KuiCTooltipTheme g_purple = {0x7c3aedffu, 0xffffffffu, 0x5b21b6ffu, 8.0f};
static KuiCTooltipTheme g_default_theme;

static void check_tooltip(void) {
    double delay = -1;
    double after = -1;
    KuiCTooltipTheme theme;
    KuiCTooltipTheme bad = g_purple;
    KuiStatus status;
    KuiStatus bad_status;
    KuiRect anchor;
    uint32_t id = 0;
    int shown;

    kui_document_get_tooltip_delay(g_doc, &delay);
    kui_document_set_tooltip_delay(g_doc, 150.0);
    kui_document_get_tooltip_delay(g_doc, &after);
    bad_status = kui_document_set_tooltip_delay(g_doc, -5.0);
    check("tooltip.delay", near_f(delay, 500.0, 0.01) && near_f(after, 150.0, 0.01) &&
              bad_status == KUI_ERR_INVALID_ARG,
          "default %.1f, after set(150) %.1f, set(-5) status %d", delay, after, bad_status);
    kui_document_set_tooltip_delay(g_doc, 500.0);

    kui_document_get_tooltip_theme(g_doc, &g_default_theme);
    kui_document_set_tooltip_theme(g_doc, &g_purple);
    memset(&theme, 0, sizeof theme);
    kui_document_get_tooltip_theme(g_doc, &theme);
    bad.radius = -1.0f;
    bad_status = kui_document_set_tooltip_theme(g_doc, &bad);
    check("tooltip.theme", themes_equal(&theme, &g_purple) && bad_status == KUI_ERR_INVALID_ARG,
          "read back %08x/%08x/%08x r%.1f, radius -1 status %d", theme.background,
          theme.foreground, theme.border, theme.radius, bad_status);
    kui_document_set_tooltip_theme(g_doc, &g_default_theme);

    rect_of("tip-anchor", &anchor);
    status = kui_document_show_tooltip(g_doc, "Tooltip from the host", 0, &anchor,
                                       KUI_C_POPUP_BOTTOM, &id);
    shown = has_type(KUI_C_POPUP_TOOLTIP);
    check("tooltip.show_tooltip", status == KUI_OK && id != 0 && shown == 1 && anchor.width > 0,
          "status %d id %u has tooltip %d anchor %.0fx%.0f", status, id, shown, anchor.width,
          anchor.height);
    bad_status = kui_document_show_tooltip(g_doc, "x", 0, &anchor, (KuiCPopupPosition)7, NULL);
    check("tooltip.bad_position", bad_status == KUI_ERR_INVALID_ARG, "status %d", bad_status);
    kui_document_dismiss_popup(g_doc, id);
    shown = has_type(KUI_C_POPUP_TOOLTIP);
    status = kui_document_dismiss_popup(g_doc, 987654u);
    check("popup.dismiss_popup", shown == 0 && status == KUI_OK,
          "has tooltip after dismiss %d, unknown id status %d", shown, status);
}

/* ---- Context menu: default menu switch, a host menu shown and dismissed by type --------- */

static int g_menu_released;
static uint32_t g_menu_id;
static char g_menu_choice[64];

static const KuiCMenuItem g_menu_items[] = {
    {"Red", 0, NULL, 0, 1, 0, 0, 0, 0, NULL, 0},
    {"Green", 0, NULL, 0, 2, 0, 0, 0, 0, NULL, 0},
    {NULL, 0, NULL, 0, 0, 1, 0, 0, 0, NULL, 0},
    {"Disabled", 0, NULL, 0, 3, 0, 1, 0, 0, NULL, 0},
};

static void on_menu_choice(uint32_t item_id, void* user_data) {
    char text[96];
    (void)user_data;
    snprintf(text, sizeof text, "chose item %u (%s)", item_id,
             item_id == 1 ? "Red" : item_id == 2 ? "Green" : "?");
    snprintf(g_menu_choice, sizeof g_menu_choice, "%u", item_id);
    kuex_set_text(g_doc, "menu-out", text);
}

static void on_menu_released(void* user_data) {
    (void)user_data;
    g_menu_released++;
}

/* Opens the host menu at #menu-spot's top-left corner. */
static KuiStatus show_menu(uint32_t* out_id) {
    KuiRect spot;
    rect_of("menu-spot", &spot);
    return kui_document_show_context_menu(g_doc, spot.x, spot.y, g_menu_items,
                                          sizeof g_menu_items / sizeof g_menu_items[0],
                                          on_menu_choice, NULL, on_menu_released, out_id);
}

static void check_menu(void) {
    int initial = -1, on = -1, off = -1;
    int released_before;
    int open;
    KuiStatus status;
    KuiStatus bad_status;
    uint32_t id = 0;

    kui_document_get_default_context_menu_enabled(g_doc, &initial);
    kui_document_set_default_context_menu_enabled(g_doc, 1);
    kui_document_get_default_context_menu_enabled(g_doc, &on);
    kui_document_set_default_context_menu_enabled(g_doc, 0);
    kui_document_get_default_context_menu_enabled(g_doc, &off);
    check("menu.default_enabled", initial == 0 && on == 1 && off == 0,
          "initial %d, after on %d, after off %d", initial, on, off);

    released_before = g_menu_released;
    status = show_menu(&id);
    open = has_type(KUI_C_POPUP_CONTEXT_MENU);
    check("menu.show_context_menu", status == KUI_OK && id != 0 && open == 1 &&
              g_menu_released == released_before,
          "status %d id %u has menu %d released %d", status, id, open, g_menu_released);
    bad_status = kui_document_show_context_menu(g_doc, 10, 10, g_menu_items, 0, on_menu_choice,
                                                NULL, NULL, NULL);
    check("menu.no_items", bad_status == KUI_ERR_INVALID_ARG, "status %d", bad_status);
    kui_document_dismiss_popups_of_type(g_doc, KUI_C_POPUP_CONTEXT_MENU);
    open = has_type(KUI_C_POPUP_CONTEXT_MENU);
    check("menu.release_fn", open == 0 && g_menu_released == released_before + 1,
          "has menu %d, release calls %d -> %d", open, released_before, g_menu_released);
}

/* ---- Popup layer: both kinds at once, removed by type, by id and all at once ------------ */

static uint32_t g_tip_id;

static void show_both(void) {
    KuiRect anchor;
    rect_of("tip-anchor", &anchor);
    kui_document_show_tooltip(g_doc, "Tooltip from the host", 0, &anchor, KUI_C_POPUP_BOTTOM,
                              &g_tip_id);
    show_menu(&g_menu_id);
}

static void check_popup_layer(void) {
    int any, tip, menu, custom;
    KuiStatus bad_status;

    show_both();
    any = any_popup();
    tip = has_type(KUI_C_POPUP_TOOLTIP);
    menu = has_type(KUI_C_POPUP_CONTEXT_MENU);
    custom = has_type(KUI_C_POPUP_CUSTOM);
    check("popup.has_popups", any == 1 && tip == 1 && menu == 1 && custom == 0,
          "any %d tooltip %d menu %d custom %d", any, tip, menu, custom);
    kui_document_dismiss_popups_of_type(g_doc, KUI_C_POPUP_TOOLTIP);
    tip = has_type(KUI_C_POPUP_TOOLTIP);
    menu = has_type(KUI_C_POPUP_CONTEXT_MENU);
    check("popup.dismiss_popups_of_type", tip == 0 && menu == 1, "tooltip %d menu %d", tip, menu);
    kui_document_dismiss_popup(g_doc, g_menu_id);
    any = any_popup();
    check("popup.dismiss_popup-menu", any == 0, "any %d", any);
    show_both();
    kui_document_dismiss_all_popups(g_doc);
    any = any_popup();
    bad_status = kui_document_dismiss_popups_of_type(g_doc, (KuiCPopupType)9);
    check("popup.dismiss_all_popups", any == 0 && bad_status == KUI_ERR_INVALID_ARG,
          "any %d, bad type status %d", any, bad_status);
}

/* ---- Input pickers: open per input type, page / time / colour, commit and close --------- */

static int g_time_changes;
static int g_time_inputs;

static void on_time_change(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    g_time_changes++;
}

static void on_time_input(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    g_time_inputs++;
}

static KuiCInputPickerState picker_state(void) {
    KuiCInputPickerState state;
    memset(&state, 0, sizeof state);
    kui_document_get_input_picker_state(g_doc, &state);
    return state;
}

/* id attribute of the picker's target input; "" when it is closed (KUI_ERR_NOT_FOUND). */
static KuiStatus picker_target(char* buf, size_t capacity) {
    KuiElement* target = NULL;
    size_t length = 0;
    KuiStatus status = kui_document_get_input_picker_target(g_doc, &target);
    buf[0] = '\0';
    if (status == KUI_OK && target) {
        kui_element_get_attribute(target, "id", 0, buf, capacity, &length);
        kui_element_release(target);
    }
    return status;
}

static KuiStatus open_picker(const char* id, KuiCPickerMode mode) {
    KuiElement* element = kuex_by_id(g_doc, id);
    KuiStatus status;
    if (!element) return KUI_ERR_NOT_FOUND;
    status = kui_element_open_input_picker(element, mode);
    kui_element_release(element);
    return status;
}

static void check_picker_date(void) {
    KuiCInputPickerState state = picker_state();
    char target[64];
    char value[64];
    KuiStatus status;
    KuiStatus target_status = picker_target(target, sizeof target);
    KuiElement* panel = NULL;

    check("picker.initial_state", !state.open && state.enabled && target_status == KUI_ERR_NOT_FOUND,
          "open %d enabled %d target status %d", state.open, state.enabled, target_status);

    status = open_picker("in-date", KUI_C_PICKER_NONE);
    state = picker_state();
    target_status = picker_target(target, sizeof target);
    kui_document_query_selector(g_doc, "kui-picker", 0, &panel);
    check("picker.open_date", status == KUI_OK && state.open && state.mode == KUI_C_PICKER_DATE &&
              state.visible_year == 2025 && state.visible_month == 3 && panel != NULL,
          "status %d open %d mode %s page %d-%d panel %d", status, state.open,
          mode_name(state.mode), state.visible_year, state.visible_month, panel != NULL);
    if (panel) kui_element_release(panel);
    check("picker.get_target", target_status == KUI_OK && strcmp(target, "in-date") == 0,
          "status %d target '%s'", target_status, target);

    kui_document_set_input_picker_month(g_doc, 2026, 7);
    state = picker_state();
    status = kui_document_set_input_picker_month(g_doc, 2026, 13);
    check("picker.set_month", state.visible_year == 2026 && state.visible_month == 7 &&
              status == KUI_ERR_INVALID_ARG,
          "page %d-%d, month 13 status %d", state.visible_year, state.visible_month, status);

    kui_document_close_input_picker(g_doc);
    state = picker_state();
    target_status = picker_target(target, sizeof target);
    input_value("in-date", value, sizeof value);
    check("picker.close", !state.open && target_status == KUI_ERR_NOT_FOUND &&
              strcmp(value, "2025-03-21") == 0,
          "open %d target status %d value '%s'", state.open, target_status, value);
}

static void check_picker_time(void) {
    KuiCInputPickerState state;
    KuiCInputPickerState after;
    KuiStatus status;
    KuiStatus bad_status;
    char value[64];

    status = open_picker("in-time", KUI_C_PICKER_TIME);
    state = picker_state();
    check("picker.open_time", status == KUI_OK && state.open && state.mode == KUI_C_PICKER_TIME &&
              state.hour == 14 && state.minute == 30,
          "status %d open %d mode %s time %d:%d", status, state.open, mode_name(state.mode),
          state.hour, state.minute);
    kui_document_set_input_picker_time(g_doc, 9, 5);
    after = picker_state();
    bad_status = kui_document_set_input_picker_time(g_doc, 24, 0);
    check("picker.set_time", after.hour == 9 && after.minute == 5 && bad_status == KUI_ERR_INVALID_ARG,
          "time %d:%d, 24:00 status %d", after.hour, after.minute, bad_status);
    kui_document_commit_input_picker(g_doc);
    state = picker_state();
    input_value("in-time", value, sizeof value);
    check("picker.commit_time", !state.open && strcmp(value, "09:05") == 0,
          "open %d value '%s'", state.open, value);
    check("picker.commit_time-events", g_time_inputs > 0 && g_time_changes > 0,
          "input %d change %d", g_time_inputs, g_time_changes);
}

static void check_picker_datetime(void) {
    KuiCInputPickerState state;
    KuiStatus status;
    char value[64];

    status = open_picker("in-dtl", KUI_C_PICKER_NONE);
    state = picker_state();
    check("picker.open_datetime_local", status == KUI_OK &&
              state.mode == KUI_C_PICKER_DATETIME_LOCAL && state.visible_year == 2025 &&
              state.visible_month == 3,
          "status %d mode %s page %d-%d", status, mode_name(state.mode), state.visible_year,
          state.visible_month);
    known("picker.open_datetime_local-time", state.hour == 14 && state.minute == 30,
          "the datetime-local panel ignores the input's time and opens at the current time",
          "time %d:%d, value 2025-03-21T14:30", state.hour, state.minute);
    kui_document_set_input_picker_month(g_doc, 2026, 7);
    kui_document_set_input_picker_time(g_doc, 8, 15);
    kui_document_commit_input_picker(g_doc);
    input_value("in-dtl", value, sizeof value);
    check("picker.commit_datetime_local", strcmp(value, "2026-07-21T08:15") == 0, "value '%s'", value);
}

static void check_picker_color(void) {
    KuiCInputPickerState state;
    KuiCInputPickerState after;
    KuiStatus status;
    KuiStatus bad_status;
    char value[64];

    status = open_picker("in-color", KUI_C_PICKER_COLOR);
    state = picker_state();
    /* #3b82f6 = rgb(59,130,246): h 217.2, s 0.760, v 0.965. */
    check("picker.open_color", status == KUI_OK && state.mode == KUI_C_PICKER_COLOR &&
              near_f(state.color_h, 217.2, 1.0) && near_f(state.color_s, 0.760, 0.01) &&
              near_f(state.color_v, 0.965, 0.01),
          "status %d mode %s hsv %.1f %.3f %.3f", status, mode_name(state.mode), state.color_h,
          state.color_s, state.color_v);
    kui_document_set_input_picker_color_hsv(g_doc, 0.0f, 1.0f, 1.0f);
    after = picker_state();
    bad_status = kui_document_set_input_picker_color_hsv(g_doc, 400.0f, 1.0f, 1.0f);
    check("picker.set_color_hsv", near_f(after.color_h, 0, 0.01) && near_f(after.color_s, 1, 0.01) &&
              near_f(after.color_v, 1, 0.01) && bad_status == KUI_ERR_INVALID_ARG,
          "hsv %.1f %.2f %.2f, h 400 status %d", after.color_h, after.color_s, after.color_v,
          bad_status);
    kui_document_commit_input_picker(g_doc);
    input_value("in-color", value, sizeof value);
    check("picker.commit_color", strcmp(value, "#ff0000") == 0, "value '%s'", value);
}

static void check_picker_refusals(void) {
    KuiStatus text_status = open_picker("in-text", KUI_C_PICKER_NONE);
    KuiStatus div_status = open_picker("not-input", KUI_C_PICKER_NONE);
    KuiStatus disabled_status;
    KuiCInputPickerState off;
    KuiCInputPickerState on;

    check("picker.no_panel", text_status == KUI_ERR_NOT_SUPPORTED && div_status == KUI_ERR_INVALID_ARG,
          "text input status %d, div status %d", text_status, div_status);
    kui_document_set_input_picker_enabled(g_doc, 0);
    off = picker_state();
    disabled_status = open_picker("in-date", KUI_C_PICKER_NONE);
    kui_document_set_input_picker_enabled(g_doc, 1);
    on = picker_state();
    check("picker.set_enabled", !off.enabled && disabled_status == KUI_ERR_NOT_SUPPORTED &&
              !off.open && on.enabled,
          "disabled %d open status %d open %d, re-enabled %d", !off.enabled, disabled_status,
          off.open, on.enabled);
}

/* ---- Final phase: dismissed toasts have finished their exit animation ------------------- */

static void phase_toasts_gone(void* user_data) {
    int count = toast_count();
    (void)user_data;
    check("toast.count_after_dismiss", count == g_toast_base, "count %d (base %d)", count,
          g_toast_base);
    finish();
}

/* ---- Panel buttons ---------------------------------------------------------------------- */

static uint32_t g_last_toast;
static int g_toast_serial;

static void report_toasts(const char* what) {
    char text[96];
    snprintf(text, sizeof text, "%s count=%d", what, toast_count());
    kuex_set_text(g_doc, "toast-out", text);
}

static void on_toast_show(KuiEvent* e, void* u) {
    char message[48];
    char what[48];
    (void)e;
    (void)u;
    g_toast_serial++;
    snprintf(message, sizeof message, "Toast #%d from the host", g_toast_serial);
    g_last_toast = show_toast(message, (KuiCToastLevel)(g_toast_serial % 4), 0);
    snprintf(what, sizeof what, "shown #%d", g_toast_serial);
    report_toasts(what);
}

static void on_toast_dismiss(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_document_dismiss_toast(g_doc, g_last_toast);
    report_toasts("dismissed last");
}

static void on_toast_clear(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_document_dismiss_all_toasts(g_doc);
    report_toasts("dismissed all");
}

static void on_toast_count(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    report_toasts("now");
}

static int g_purple_on;
static int g_delay_step;

static void report_tooltip(void) {
    char text[128];
    KuiCTooltipTheme theme;
    double delay = 0;
    memset(&theme, 0, sizeof theme);
    kui_document_get_tooltip_theme(g_doc, &theme);
    kui_document_get_tooltip_delay(g_doc, &delay);
    snprintf(text, sizeof text, "theme=%s bg=#%06x delay=%.0f tooltip=%d",
             g_purple_on ? "purple" : "default", theme.background >> 8, delay,
             has_type(KUI_C_POPUP_TOOLTIP));
    kuex_set_text(g_doc, "tip-out", text);
}

static void on_tip_show(KuiEvent* e, void* u) {
    KuiRect anchor;
    (void)e;
    (void)u;
    rect_of("tip-anchor", &anchor);
    kui_document_show_tooltip(g_doc, "Tooltip from the host", 0, &anchor, KUI_C_POPUP_BOTTOM,
                              &g_tip_id);
    report_tooltip();
}

static void on_tip_hide(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_document_dismiss_popup(g_doc, g_tip_id);
    report_tooltip();
}

static void on_tip_theme(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    g_purple_on = !g_purple_on;
    kui_document_set_tooltip_theme(g_doc, g_purple_on ? &g_purple : &g_default_theme);
    on_tip_show(e, u); /* the click closed the tooltip: show it again in the new colours */
}

static void on_tip_delay(KuiEvent* e, void* u) {
    static const double delays[3] = {500.0, 150.0, 1000.0};
    (void)e;
    (void)u;
    g_delay_step = (g_delay_step + 1) % 3;
    kui_document_set_tooltip_delay(g_doc, delays[g_delay_step]);
    report_tooltip();
}

static void on_menu_show(KuiEvent* e, void* u) {
    char text[64];
    (void)e;
    (void)u;
    show_menu(&g_menu_id);
    snprintf(text, sizeof text, "menu open=%d", has_type(KUI_C_POPUP_CONTEXT_MENU));
    kuex_set_text(g_doc, "menu-out", text);
}

static void on_menu_default(KuiEvent* e, void* u) {
    int enabled = 0;
    (void)e;
    (void)u;
    kui_document_get_default_context_menu_enabled(g_doc, &enabled);
    kui_document_set_default_context_menu_enabled(g_doc, !enabled);
    kui_document_get_default_context_menu_enabled(g_doc, &enabled);
    kuex_set_text(g_doc, "menu-out", enabled ? "default=on" : "default=off");
}

static int g_context_custom;

static void report_context_menu(void* user_data) {
    char text[96];
    (void)user_data;
    snprintf(text, sizeof text, "contextmenu: %s menu open=%d",
             g_context_custom ? "custom" : "default", has_type(KUI_C_POPUP_CONTEXT_MENU));
    kuex_set_text(g_doc, "menu-out", text);
}

/* Right-click: the built-in menu when it is enabled, the host's own menu otherwise. */
static void on_context_menu(KuiEvent* e, void* u) {
    int enabled = 0;
    KuiTimerId timer;
    (void)u;
    kui_document_get_default_context_menu_enabled(g_doc, &enabled);
    g_context_custom = !enabled;
    if (!enabled) {
        kui_event_prevent_default(e);
        show_menu(&g_menu_id);
    }
    kui_platform_set_timeout(report_context_menu, NULL, 100, &timer);
}

/* A click anywhere dismisses open popups first, so each button shows both kinds again and
 * then applies its own removal; the report is "before -> after". */
static void popup_counts(char* buf, size_t capacity) {
    snprintf(buf, capacity, "popups=%d tooltip=%d menu=%d", any_popup(),
             has_type(KUI_C_POPUP_TOOLTIP), has_type(KUI_C_POPUP_CONTEXT_MENU));
}

static void report_popups(const char* before) {
    char after[64];
    char text[160];
    popup_counts(after, sizeof after);
    snprintf(text, sizeof text, "%s%s%s", before ? before : "", before ? " -> " : "", after);
    kuex_set_text(g_doc, "pop-out", text);
}

static void on_pop_both(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    show_both();
    report_popups(NULL);
}

static void on_pop_tip_off(KuiEvent* e, void* u) {
    char before[64];
    (void)e;
    (void)u;
    show_both();
    popup_counts(before, sizeof before);
    kui_document_dismiss_popups_of_type(g_doc, KUI_C_POPUP_TOOLTIP);
    report_popups(before);
}

static void on_pop_menu_id(KuiEvent* e, void* u) {
    char before[64];
    (void)e;
    (void)u;
    show_both();
    popup_counts(before, sizeof before);
    kui_document_dismiss_popup(g_doc, g_menu_id);
    report_popups(before);
}

static void on_pop_clear(KuiEvent* e, void* u) {
    char before[64];
    (void)e;
    (void)u;
    show_both();
    popup_counts(before, sizeof before);
    kui_document_dismiss_all_popups(g_doc);
    report_popups(before);
}

static void report_picker(const char* what) {
    char text[160];
    char target[64];
    KuiCInputPickerState state = picker_state();
    picker_target(target, sizeof target);
    snprintf(text, sizeof text, "%sopen=%d enabled=%d mode=%s page=%04d-%02d target=%s", what,
             state.open, state.enabled, mode_name(state.mode), state.visible_year,
             state.visible_month, target[0] ? target : "-");
    kuex_set_text(g_doc, "pick-out", text);
}

static void report_values(void) {
    char text[200];
    char time_value[32], dtl[48], color[32];
    input_value("in-time", time_value, sizeof time_value);
    input_value("in-dtl", dtl, sizeof dtl);
    input_value("in-color", color, sizeof color);
    snprintf(text, sizeof text, "time=%s dtl=%s color=%s", time_value, dtl, color);
    kuex_set_text(g_doc, "pick-values", text);
}

static void on_pick_date(KuiEvent* e, void* u) {
    KuiStatus status;
    char what[48];
    (void)e;
    (void)u;
    status = open_picker("in-date", KUI_C_PICKER_NONE);
    snprintf(what, sizeof what, "status=%s ", status_name(status));
    report_picker(what);
}

static void on_pick_close(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_document_close_input_picker(g_doc);
    report_picker("");
}

static void on_pick_time(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    if (open_picker("in-time", KUI_C_PICKER_TIME) == KUI_OK) {
        kui_document_set_input_picker_time(g_doc, 18, 45);
        kui_document_commit_input_picker(g_doc);
    }
    report_picker("");
    report_values();
}

static void on_pick_dtl(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    if (open_picker("in-dtl", KUI_C_PICKER_DATETIME_LOCAL) == KUI_OK) {
        kui_document_set_input_picker_month(g_doc, 2027, 1);
        kui_document_set_input_picker_time(g_doc, 22, 10);
        kui_document_commit_input_picker(g_doc);
    }
    report_picker("");
    report_values();
}

static void on_pick_color(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    if (open_picker("in-color", KUI_C_PICKER_COLOR) == KUI_OK) {
        kui_document_set_input_picker_color_hsv(g_doc, 120.0f, 1.0f, 1.0f);
        kui_document_commit_input_picker(g_doc);
    }
    report_picker("");
    report_values();
}

static void on_pick_toggle(KuiEvent* e, void* u) {
    KuiCInputPickerState state = picker_state();
    KuiStatus status;
    char what[48];
    (void)e;
    (void)u;
    kui_document_set_input_picker_enabled(g_doc, !state.enabled);
    status = open_picker("in-date", KUI_C_PICKER_NONE);
    if (status == KUI_OK) kui_document_close_input_picker(g_doc);
    snprintf(what, sizeof what, "open(date) status=%s ", status_name(status));
    report_picker(what);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    int failed = 0;
    KuiTimerId timer;
    g_doc = host->document;

    failed |= kuex_on(g_doc, "in-time", "change", on_time_change, NULL);
    failed |= kuex_on(g_doc, "in-time", "input", on_time_input, NULL);

    check_toasts();
    check_tooltip();
    check_menu();
    check_popup_layer();
    check_picker_date();
    check_picker_time();
    check_picker_datetime();
    check_picker_color();
    check_picker_refusals();
    report_values();
    /* Toast exit animations take a few hundred milliseconds. */
    kui_platform_set_timeout(phase_toasts_gone, NULL, 1200, &timer);

    failed |= kuex_on(g_doc, "btn-toast-show", "click", on_toast_show, NULL);
    failed |= kuex_on(g_doc, "btn-toast-dismiss", "click", on_toast_dismiss, NULL);
    failed |= kuex_on(g_doc, "btn-toast-clear", "click", on_toast_clear, NULL);
    failed |= kuex_on(g_doc, "btn-toast-count", "click", on_toast_count, NULL);
    failed |= kuex_on(g_doc, "btn-tip-show", "click", on_tip_show, NULL);
    failed |= kuex_on(g_doc, "btn-tip-hide", "click", on_tip_hide, NULL);
    failed |= kuex_on(g_doc, "btn-tip-theme", "click", on_tip_theme, NULL);
    failed |= kuex_on(g_doc, "btn-tip-delay", "click", on_tip_delay, NULL);
    failed |= kuex_on(g_doc, "btn-menu-show", "click", on_menu_show, NULL);
    failed |= kuex_on(g_doc, "btn-menu-default", "click", on_menu_default, NULL);
    failed |= kuex_on(g_doc, "menu-spot", "contextmenu", on_context_menu, NULL);
    failed |= kuex_on(g_doc, "btn-pop-both", "click", on_pop_both, NULL);
    failed |= kuex_on(g_doc, "btn-pop-tip-off", "click", on_pop_tip_off, NULL);
    failed |= kuex_on(g_doc, "btn-pop-menu-id", "click", on_pop_menu_id, NULL);
    failed |= kuex_on(g_doc, "btn-pop-clear", "click", on_pop_clear, NULL);
    failed |= kuex_on(g_doc, "btn-pick-date", "click", on_pick_date, NULL);
    failed |= kuex_on(g_doc, "btn-pick-close", "click", on_pick_close, NULL);
    failed |= kuex_on(g_doc, "btn-pick-time", "click", on_pick_time, NULL);
    failed |= kuex_on(g_doc, "btn-pick-dtl", "click", on_pick_dtl, NULL);
    failed |= kuex_on(g_doc, "btn-pick-color", "click", on_pick_color, NULL);
    failed |= kuex_on(g_doc, "btn-pick-toggle", "click", on_pick_toggle, NULL);
    return failed;
}
