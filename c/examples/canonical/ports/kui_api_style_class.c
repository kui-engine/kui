/* API tour: classes, inline style, computed style, stylesheets and animation state
 * (examples/api_tours/style_class). On start the host calls every function once, checks the
 * result against what kui_c.h documents and logs one row per check into #tour-log. Animation
 * state is only observable once frames run, so those checks finish from a 400 ms timer and
 * #tour-result becomes "PASS n/n" then. The panel buttons repeat the calls by hand. */
#include "kui_c_example_dom.h"

#include <stdarg.h>
#include <stdlib.h>

static KuiDocument* g_doc;

/* ---- Self-check bookkeeping: count, log one <tr> per check, publish the verdict ----------
 * check(): a documented behaviour; a failure makes the tour FAIL. (No known engine bugs in this
 * tour; see kui_api_dom_tree.c for the known() variant that logs a reported bug without failing.) */

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
    kui_element_class_list_add(result, g_failed == 0 ? "pass" : "fail", 0);
    kui_element_release(result);
}

/* ---- Small readers ----------------------------------------------------------------------- */

static void release(KuiElement* element) {
    if (element) kui_element_release(element);
}

static void class_of(KuiElement* element, char* buf, size_t capacity) {
    size_t length = 0;
    buf[0] = '\0';
    if (element) kui_element_get_class_name(element, buf, capacity, &length);
}

static int contains(KuiElement* element, const char* token) {
    int yes = -1;
    if (element) kui_element_class_list_contains(element, token, 0, &yes);
    return yes;
}

/* Inline property value; returns the status (KUI_ERR_NOT_FOUND when not set). */
static KuiStatus inline_value(KuiElement* element, const char* property, char* buf, size_t capacity) {
    size_t length = 0;
    buf[0] = '\0';
    if (!element) return KUI_ERR_INVALID_ARG;
    return kui_element_get_style_property(element, property, 0, buf, capacity, &length);
}

static size_t property_count(KuiElement* element) {
    size_t count = 0;
    if (element) kui_element_get_style_property_count(element, &count);
    return count;
}

static void style_text_of(KuiElement* element, char* buf, size_t capacity) {
    size_t length = 0;
    buf[0] = '\0';
    if (element) kui_element_get_style_text(element, buf, capacity, &length);
}

static void computed(KuiElement* element, const char* property, char* buf, size_t capacity) {
    size_t length = 0;
    buf[0] = '\0';
    if (element) kui_element_get_computed_style_value(element, property, 0, buf, capacity, &length);
}

static void computed_by_id(const char* id, const char* property, char* buf, size_t capacity) {
    KuiElement* element = kuex_by_id(g_doc, id);
    computed(element, property, buf, capacity);
    release(element);
}

/* "name=value[ !important], ..." for every inline property, prefixed by the count. */
static void describe_inline(KuiElement* element, char* out, size_t capacity) {
    size_t count = property_count(element);
    size_t index;
    size_t used;
    char name[64];
    char value[64];
    size_t length = 0;
    int important = 0;
    snprintf(out, capacity, "%zu:", count);
    for (index = 0; index < count; ++index) {
        name[0] = '\0';
        kui_element_get_style_property_name(element, index, name, sizeof name, &length);
        inline_value(element, name, value, sizeof value);
        important = 0;
        kui_element_style_property_is_important(element, name, 0, &important);
        used = strlen(out);
        snprintf(out + used, capacity - used, "%s %s=%s%s", index ? "," : "", name, value,
                 important ? " !important" : "");
    }
}

/* The page's own <style> text, read from <head>; malloc'd (free it), NULL when absent. */
static char* page_css(void) {
    KuiElement* head = NULL;
    KuiElement* style = NULL;
    size_t need = 0;
    char* css = NULL;
    if (kui_document_head(g_doc, &head) == KUI_OK &&
        kui_element_query_selector(head, "style", 0, &style) == KUI_OK &&
        kui_element_get_text_content(style, NULL, 0, &need) == KUI_OK && need > 0) {
        css = (char*)malloc(need + 1);
        if (css && kui_element_get_text_content(style, css, need + 1, &need) != KUI_OK) {
            free(css);
            css = NULL;
        }
    }
    release(style);
    release(head);
    return css;
}

/* ---- Class list: add / remove / toggle / contains, class name and id -------------------- */

static void check_class_list(void) {
    KuiElement* el = kuex_by_id(g_doc, "sc-cls");
    char buf[128];
    size_t length = 0;
    int on, off;

    buf[0] = '\0';
    if (el) kui_element_get_id(el, buf, sizeof buf, &length);
    check("el.get_id", strcmp(buf, "sc-cls") == 0, "id '%s'", buf);
    class_of(el, buf, sizeof buf);
    check("el.get_class_name", strcmp(buf, "a b") == 0, "className '%s'", buf);

    if (el) kui_element_class_list_add(el, "c", 0);
    class_of(el, buf, sizeof buf);
    check("el.class_list_add", strcmp(buf, "a b c") == 0 && contains(el, "c") == 1,
          "className '%s'", buf);
    if (el) kui_element_class_list_add(el, "a", 0);
    class_of(el, buf, sizeof buf);
    check("el.class_list_add-duplicate", strcmp(buf, "a b c") == 0, "className '%s'", buf);

    if (el) kui_element_class_list_remove(el, "b", 0);
    class_of(el, buf, sizeof buf);
    check("el.class_list_remove", strcmp(buf, "a c") == 0 && contains(el, "b") == 0,
          "className '%s', contains(b) %d", buf, contains(el, "b"));
    check("el.class_list_contains", contains(el, "a") == 1 && contains(el, "zz") == 0,
          "contains(a) %d, contains(zz) %d", contains(el, "a"), contains(el, "zz"));

    if (el) kui_element_class_list_toggle(el, "d", 0);
    on = contains(el, "d");
    if (el) kui_element_class_list_toggle(el, "d", 0);
    off = contains(el, "d");
    check("el.class_list_toggle", on == 1 && off == 0, "after toggles %d then %d", on, off);

    if (el) kui_element_set_class_name(el, "tour-wide", 0);
    class_of(el, buf, sizeof buf);
    check("el.set_class_name", strcmp(buf, "tour-wide") == 0 && contains(el, "a") == 0,
          "className '%s'", buf);
    /* The page stylesheet gives #sc-cls.tour-wide a width of 77px. */
    computed(el, "width", buf, sizeof buf);
    check("el.class_drives_style", strcmp(buf, "77px") == 0, "computed width '%s'", buf);
    release(el);
}

/* ---- Inline style: properties, !important, enumeration, whole-declaration text ---------- */

static void check_inline_style(void) {
    KuiElement* el = kuex_by_id(g_doc, "sc-style");
    KuiStatus status;
    char buf[256];
    char names[128] = "";
    int important = -1;
    size_t index;
    size_t length = 0;
    size_t count;

    status = inline_value(el, "color", buf, sizeof buf);
    check("el.get_style_property-missing", status == KUI_ERR_NOT_FOUND, "status %d", status);

    /* kui_c.h: named colours come back serialized as hex, lengths keep their unit. */
    if (el) kui_element_set_style_property(el, "color", 0, "red", 0, 0);
    status = inline_value(el, "color", buf, sizeof buf);
    check("el.set_get_style_property", status == KUI_OK && strcmp(buf, "#ff0000") == 0,
          "status %d, color '%s'", status, buf);
    if (el) kui_element_set_style_property(el, "width", 0, "100px", 0, 0);
    inline_value(el, "width", buf, sizeof buf);
    check("el.get_style_property-length", strcmp(buf, "100px") == 0, "width '%s'", buf);

    if (el) kui_element_style_property_is_important(el, "color", 0, &important);
    {
        int after = -1;
        if (el) {
            kui_element_set_style_property(el, "background-color", 0, "#00ff00", 0, 1);
            kui_element_style_property_is_important(el, "background-color", 0, &after);
        }
        check("el.style_property_is_important", important == 0 && after == 1,
              "color %d, background-color %d", important, after);
    }

    count = property_count(el);
    for (index = 0; index < count; ++index) {
        size_t used = strlen(names);
        buf[0] = '\0';
        kui_element_get_style_property_name(el, index, buf, sizeof buf, &length);
        snprintf(names + used, sizeof names - used, "%s%s", index ? "," : "", buf);
    }
    check("el.get_style_property_count_name",
          count == 3 && strstr(names, "color") && strstr(names, "width") &&
          strstr(names, "background-color"), "count %zu, names '%s'", count, names);

    style_text_of(el, buf, sizeof buf);
    check("el.get_style_text", strstr(buf, "width") && strstr(buf, "100px") &&
          strstr(buf, "background-color") && strstr(buf, "!important"), "cssText '%s'", buf);

    /* set_style_text replaces the whole declaration. */
    if (el) kui_element_set_style_text(el, "margin-left: 4px; padding-top: 2px", 0);
    inline_value(el, "margin-left", buf, sizeof buf);
    status = inline_value(el, "color", names, sizeof names);
    check("el.set_style_text", property_count(el) == 2 && strcmp(buf, "4px") == 0 &&
          status == KUI_ERR_NOT_FOUND, "count %zu, margin-left '%s', color status %d",
          property_count(el), buf, status);

    if (el) kui_element_remove_style_property(el, "margin-left", 0);
    status = inline_value(el, "margin-left", buf, sizeof buf);
    check("el.remove_style_property", status == KUI_ERR_NOT_FOUND && property_count(el) == 1,
          "status %d, count %zu", status, property_count(el));
    release(el);
}

/* ---- Computed style: the cascade's resolved values -------------------------------------- */

static void check_computed(void) {
    KuiElement* el = kuex_by_id(g_doc, "cs-target");
    char display[32], width[32], font[32], padding[32], unknown[32];
    KuiStatus status = KUI_ERR_INVALID_ARG;
    size_t length = 0;

    computed(el, "display", display, sizeof display);
    computed(el, "width", width, sizeof width);
    computed(el, "font-size", font, sizeof font);
    computed(el, "padding-left", padding, sizeof padding);
    check("el.get_computed_style_value", strcmp(display, "flex") == 0 &&
          strcmp(width, "240px") == 0 && strcmp(font, "15px") == 0 && strcmp(padding, "8px") == 0,
          "display '%s', width '%s', font-size '%s', padding-left '%s'", display, width, font,
          padding);
    unknown[0] = 'x';
    unknown[1] = '\0';
    if (el) status = kui_element_get_computed_style_value(el, "no-such-property", 0, unknown,
                                                          sizeof unknown, &length);
    check("el.get_computed_style_value-unknown", status == KUI_OK && unknown[0] == '\0',
          "status %d, value '%s'", status, unknown);

    /* Inline style feeds the cascade too. */
    computed_by_id("sc-style", "padding-top", padding, sizeof padding);
    check("el.get_computed_style_value-inline", strcmp(padding, "2px") == 0,
          "padding-top '%s'", padding);
    release(el);
}

/* ---- Stylesheets & profile: add, clear every author sheet, restore the page's own -------- */

static void check_stylesheets(void) {
    KuiCStyleProfile profile = (KuiCStyleProfile)-1;
    KuiStatus status;
    char width[32], display[32];
    char* css = page_css();

    status = kui_document_get_style_profile(g_doc, &profile);
    check("doc.get_style_profile", status == KUI_OK && profile == KUI_C_STYLE_NATIVE_GUI,
          "status %d, profile %d", status, (int)profile);

    status = kui_document_add_stylesheet(g_doc, "#sc-sheet { width: 123px; }", 0);
    computed_by_id("sc-sheet", "width", width, sizeof width);
    check("doc.add_stylesheet", status == KUI_OK && strcmp(width, "123px") == 0,
          "status %d, width '%s'", status, width);

    check("head.page_css", css && strstr(css, "#cs-target"), "page <style> text %s",
          css ? "lacks #cs-target" : "missing");

    /* Clearing drops the page sheet as well as ours: #cs-target falls back to display:block. */
    status = kui_document_clear_stylesheets(g_doc);
    computed_by_id("cs-target", "display", display, sizeof display);
    computed_by_id("sc-sheet", "width", width, sizeof width);
    check("doc.clear_stylesheets", status == KUI_OK && strcmp(display, "block") == 0 &&
          strcmp(width, "123px") != 0, "status %d, display '%s', width '%s'", status, display,
          width);

    status = css ? kui_document_add_stylesheet(g_doc, css, 0) : KUI_ERR_NOT_FOUND;
    computed_by_id("cs-target", "display", display, sizeof display);
    computed_by_id("sc-sheet", "width", width, sizeof width);
    check("doc.add_stylesheet-restore", status == KUI_OK && strcmp(display, "flex") == 0 &&
          strcmp(width, "123px") != 0, "status %d, display '%s', width '%s'", status, display,
          width);
    free(css);
}

/* ---- Animation state: needs running frames, so it is checked from a timer --------------- */

static void check_animation(void* user_data) {
    KuiElement* box = kuex_by_id(g_doc, "anim-box");
    KuiElement* bar = kuex_by_id(g_doc, "sc-trans");
    KuiElement* idle = kuex_by_id(g_doc, "cls-target");
    int animating = -1, transitioning = -1;
    uint32_t animations = 0, transitions = 0;
    (void)user_data;

    if (box) kui_element_get_animation_state(box, &animating, &transitioning);
    check("el.get_animation_state-animation", animating == 1, "animating %d, transitioning %d",
          animating, transitioning);
    animating = transitioning = -1;
    if (bar) kui_element_get_animation_state(bar, &animating, &transitioning);
    check("el.get_animation_state-transition", transitioning == 1,
          "animating %d, transitioning %d", animating, transitioning);
    animating = transitioning = -1;
    if (idle) kui_element_get_animation_state(idle, &animating, NULL);
    if (idle) kui_element_get_animation_state(idle, NULL, &transitioning);
    check("el.get_animation_state-idle", animating == 0 && transitioning == 0,
          "animating %d, transitioning %d", animating, transitioning);
    kui_document_get_animation_counts(g_doc, &animations, &transitions);
    check("doc.get_animation_counts", animations >= 1 && transitions >= 1,
          "animations %u, transitions %u", animations, transitions);

    release(box);
    release(bar);
    release(idle);
    finish();
}

/* Starts the #anim-box animation (class .spinning) and a 2 s width transition on #sc-trans
 * (inline style beats the 40px sheet value). Runs after the stylesheet checks only so that those
 * checks see a quiet page; clearing the sheet that holds a running animation's @keyframes is safe
 * (the engine cancels that animation: test_keyframes_stylesheet_removal). */
static void start_animation_checks(void) {
    KuiElement* box = kuex_by_id(g_doc, "anim-box");
    char width[32];
    KuiTimerId timer = 0;
    if (box) kui_element_class_list_add(box, "spinning", 0);
    release(box);
    computed_by_id("sc-trans", "width", width, sizeof width); /* settle the old value first */
    kuex_set_style(g_doc, "sc-trans", "width", "200px");
    if (kui_platform_set_timeout(check_animation, NULL, 400, &timer) != KUI_OK) {
        check("animation.timer", 0, "kui_platform_set_timeout failed");
        finish();
    }
}

/* ---- Panel buttons ----------------------------------------------------------------------- */

static void show_classes(void) {
    KuiElement* el = kuex_by_id(g_doc, "cls-target");
    char id[64], cls[128], out[256];
    size_t length = 0;
    id[0] = '\0';
    if (el) kui_element_get_id(el, id, sizeof id, &length);
    class_of(el, cls, sizeof cls);
    snprintf(out, sizeof out, "#%s class=\"%s\" | accent=%s", id, cls,
             contains(el, "accent") == 1 ? "yes" : "no");
    kuex_set_text(g_doc, "cls-out", out);
    release(el);
}

static void on_cls_toggle(KuiEvent* event, void* user_data) {
    KuiElement* el = kuex_by_id(g_doc, "cls-target");
    (void)event;
    (void)user_data;
    if (el) kui_element_class_list_toggle(el, "accent", 0);
    release(el);
    show_classes();
}

static void on_cls_add(KuiEvent* event, void* user_data) {
    KuiElement* el = kuex_by_id(g_doc, "cls-target");
    (void)event;
    (void)user_data;
    if (el) kui_element_class_list_add(el, "big", 0);
    release(el);
    show_classes();
}

static void on_cls_remove(KuiEvent* event, void* user_data) {
    KuiElement* el = kuex_by_id(g_doc, "cls-target");
    (void)event;
    (void)user_data;
    if (el) kui_element_class_list_remove(el, "big", 0);
    release(el);
    show_classes();
}

static void on_cls_reset(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    kuex_set_class(g_doc, "cls-target", "card");
    show_classes();
}

static void show_style_text(void) {
    KuiElement* el = kuex_by_id(g_doc, "st-target");
    char text[256];
    style_text_of(el, text, sizeof text);
    kuex_set_text(g_doc, "st-out", text[0] ? text : "(no inline style)");
    release(el);
}

static void on_st_set(KuiEvent* event, void* user_data) {
    KuiElement* el = kuex_by_id(g_doc, "st-target");
    (void)event;
    (void)user_data;
    if (el) {
        kui_element_set_style_property(el, "color", 0, "#b91c1c", 0, 0);
        kui_element_set_style_property(el, "background-color", 0, "#fee2e2", 0, 1);
    }
    release(el);
    show_style_text();
}

static void on_st_text(KuiEvent* event, void* user_data) {
    KuiElement* el = kuex_by_id(g_doc, "st-target");
    (void)event;
    (void)user_data;
    if (el) kui_element_set_style_text(el, "background-color: #fef3c7; padding: 12px", 0);
    release(el);
    show_style_text();
}

static void on_st_remove(KuiEvent* event, void* user_data) {
    KuiElement* el = kuex_by_id(g_doc, "st-target");
    (void)event;
    (void)user_data;
    if (el) kui_element_remove_style_property(el, "background-color", 0);
    release(el);
    show_style_text();
}

static void on_st_list(KuiEvent* event, void* user_data) {
    KuiElement* el = kuex_by_id(g_doc, "st-target");
    char out[512];
    (void)event;
    (void)user_data;
    describe_inline(el, out, sizeof out);
    kuex_set_text(g_doc, "st-out", out);
    release(el);
}

static void on_cs_read(KuiEvent* event, void* user_data) {
    KuiElement* el = kuex_by_id(g_doc, "cs-target");
    char display[32], width[32], font[32], color[64], out[256];
    (void)event;
    (void)user_data;
    computed(el, "display", display, sizeof display);
    computed(el, "width", width, sizeof width);
    computed(el, "font-size", font, sizeof font);
    computed(el, "color", color, sizeof color);
    snprintf(out, sizeof out, "display=%s | width=%s | font-size=%s | color=%s", display, width,
             font, color);
    kuex_set_text(g_doc, "cs-out", out);
    release(el);
}

static void on_cs_wide(KuiEvent* event, void* user_data) {
    KuiElement* el = kuex_by_id(g_doc, "cs-target");
    if (el) kui_element_class_list_toggle(el, "wide", 0);
    release(el);
    on_cs_read(event, user_data);
}

static void on_sheet_add(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    kui_document_add_stylesheet(
        g_doc, "#sheet-probe { background-color: #dcfce7; border-left-color: #16a34a; }", 0);
    kuex_set_text(g_doc, "sheet-out", "host sheet added");
}

/* Safe while #anim-box is animating: the animation whose @keyframes live in a cleared sheet is
 * cancelled, and the restored page sheet starts it again. */
static void on_sheet_clear(KuiEvent* event, void* user_data) {
    char* css = page_css();
    (void)event;
    (void)user_data;
    kui_document_clear_stylesheets(g_doc);
    if (css) kui_document_add_stylesheet(g_doc, css, 0);
    kuex_set_text(g_doc, "sheet-out", css ? "cleared all sheets, page sheet restored"
                                          : "cleared all sheets (page <style> not found)");
    free(css);
}

static void on_sheet_profile(KuiEvent* event, void* user_data) {
    KuiCStyleProfile profile = KUI_C_STYLE_NATIVE_GUI;
    (void)event;
    (void)user_data;
    kui_document_get_style_profile(g_doc, &profile);
    kuex_set_text(g_doc, "sheet-out", profile == KUI_C_STYLE_BROWSER_COMPAT
                                          ? "style profile = browser-compat"
                                          : "style profile = native-gui");
}

static void on_anim_toggle(KuiEvent* event, void* user_data) {
    KuiElement* el = kuex_by_id(g_doc, "anim-box");
    (void)event;
    (void)user_data;
    if (el) kui_element_class_list_toggle(el, "spinning", 0);
    release(el);
}

static void on_trans_go(KuiEvent* event, void* user_data) {
    KuiElement* el = kuex_by_id(g_doc, "trans-box");
    (void)event;
    (void)user_data;
    if (el) kui_element_class_list_toggle(el, "wide", 0);
    release(el);
}

static void on_anim_read(KuiEvent* event, void* user_data) {
    KuiElement* box = kuex_by_id(g_doc, "anim-box");
    KuiElement* bar = kuex_by_id(g_doc, "trans-box");
    int animating = 0, transitioning = 0;
    uint32_t animations = 0, transitions = 0;
    char out[160];
    (void)event;
    (void)user_data;
    if (box) kui_element_get_animation_state(box, &animating, NULL);
    if (bar) kui_element_get_animation_state(bar, NULL, &transitioning);
    kui_document_get_animation_counts(g_doc, &animations, &transitions);
    snprintf(out, sizeof out, "box: animating=%s | bar: transitioning=%s | doc: animations=%u transitions=%u",
             animating ? "yes" : "no", transitioning ? "yes" : "no", animations, transitions);
    kuex_set_text(g_doc, "anim-out", out);
    release(box);
    release(bar);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    int failed = 0;
    g_doc = host->document;

    check_class_list();
    check_inline_style();
    check_computed();
    check_stylesheets();
    start_animation_checks(); /* finishes the verdict from its timer */

    failed |= kuex_on(g_doc, "btn-cls-toggle", "click", on_cls_toggle, NULL);
    failed |= kuex_on(g_doc, "btn-cls-add", "click", on_cls_add, NULL);
    failed |= kuex_on(g_doc, "btn-cls-remove", "click", on_cls_remove, NULL);
    failed |= kuex_on(g_doc, "btn-cls-reset", "click", on_cls_reset, NULL);
    failed |= kuex_on(g_doc, "btn-st-set", "click", on_st_set, NULL);
    failed |= kuex_on(g_doc, "btn-st-text", "click", on_st_text, NULL);
    failed |= kuex_on(g_doc, "btn-st-remove", "click", on_st_remove, NULL);
    failed |= kuex_on(g_doc, "btn-st-list", "click", on_st_list, NULL);
    failed |= kuex_on(g_doc, "btn-cs-read", "click", on_cs_read, NULL);
    failed |= kuex_on(g_doc, "btn-cs-wide", "click", on_cs_wide, NULL);
    failed |= kuex_on(g_doc, "btn-sheet-add", "click", on_sheet_add, NULL);
    failed |= kuex_on(g_doc, "btn-sheet-clear", "click", on_sheet_clear, NULL);
    failed |= kuex_on(g_doc, "btn-sheet-profile", "click", on_sheet_profile, NULL);
    failed |= kuex_on(g_doc, "btn-anim-toggle", "click", on_anim_toggle, NULL);
    failed |= kuex_on(g_doc, "btn-trans-go", "click", on_trans_go, NULL);
    failed |= kuex_on(g_doc, "btn-anim-read", "click", on_anim_read, NULL);
    return failed;
}
