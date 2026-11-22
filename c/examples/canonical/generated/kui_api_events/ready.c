/* Copied by scripts/generate_c_examples.py from examples/c/ports/kui_api_events.c; edit that file, not this copy. */
/* API tour: DOM events from the host (examples/api_tours/events). On start the host registers
 * listeners, dispatches synthetic events (kui_element_dispatch_event) and injects real mouse and
 * keyboard input (kui_devtools_dispatch_mouse_event / _key_event, the same dispatch path as the
 * OS), then checks what every listener saw: propagation order, current target, stop_propagation,
 * flags and prevent_default, listener removal, mouse button and coordinates, related targets,
 * key / code / modifiers, focus events and the whole drag-and-drop data API. One row per check
 * goes into #tour-log; #tour-result ends as "PASS n/n". Afterwards the panels react to real
 * input: every listener writes what it read into the panel's output line. */
#include "kui_c_example_dom.h"

#include <math.h>
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

/* ---- Small helpers ------------------------------------------------------------------------ */

static void release(KuiElement* element) {
    if (element) kui_element_release(element);
}

static int near(float a, float b) {
    return fabsf(a - b) <= 0.51f;
}

/* Appends ",text" (or "text" when empty) to a fixed buffer. */
static void append(char* buf, size_t capacity, const char* text) {
    size_t used = strlen(buf);
    snprintf(buf + used, capacity - used, "%s%s", used ? "," : "", text);
}

static void id_of(KuiElement* element, char* buf, size_t capacity) {
    size_t length = 0;
    buf[0] = '\0';
    if (element) kui_element_get_id(element, buf, capacity, &length);
}

/* The id of the event's current target / target / related target ("" when there is none). */
static void current_id(KuiEvent* event, char* buf, size_t capacity) {
    KuiElement* element = NULL;
    kui_event_get_current_target(event, &element);
    id_of(element, buf, capacity);
    release(element);
}

static void target_id(KuiEvent* event, char* buf, size_t capacity) {
    KuiElement* element = NULL;
    kui_event_get_target(event, &element);
    id_of(element, buf, capacity);
    release(element);
}

static void related_id(KuiEvent* event, char* buf, size_t capacity) {
    KuiElement* element = NULL;
    kui_event_get_related_target(event, &element);
    id_of(element, buf, capacity);
    release(element);
}

static void type_of(KuiEvent* event, char* buf, size_t capacity) {
    size_t length = 0;
    buf[0] = '\0';
    kui_event_get_type(event, buf, capacity, &length);
}

static KuiRect rect_of(const char* id) {
    KuiRect rect;
    KuiElement* element = kuex_by_id(g_doc, id);
    memset(&rect, 0, sizeof rect);
    if (element) kui_element_get_bounding_rect(element, &rect);
    release(element);
    return rect;
}

static KuiStatus dispatch(const char* id, const char* name, int bubbles, int cancelable,
                          int* not_canceled) {
    KuiElement* element = kuex_by_id(g_doc, id);
    KuiStatus status = KUI_ERR_NOT_FOUND;
    if (element) status = kui_element_dispatch_event(element, name, 0, bubbles, cancelable, not_canceled);
    release(element);
    return status;
}

static void listen(const char* id, const char* name, KuiEventListenerFn fn, void* user_data) {
    if (kuex_on(g_doc, id, name, fn, user_data) != 0) {
        fprintf(stderr, "kui_api_events: cannot listen for %s on #%s\n", name, id);
    }
}

/* Injected real input, in viewport coordinates. */
static void mouse(KuiCMouseEventType type, float x, float y, KuiCMouseButton button, int clicks) {
    kui_devtools_dispatch_mouse_event(g_doc, type, x, y, button, 0, clicks, 0, 0);
}

static void click_at(const char* id, KuiCMouseButton button, int clicks) {
    KuiRect rect = rect_of(id);
    float x = rect.x + rect.width / 2;
    float y = rect.y + rect.height / 2;
    mouse(KUI_C_MOUSE_MOVED, x, y, KUI_C_MOUSE_BUTTON_NONE, 0);
    mouse(KUI_C_MOUSE_PRESSED, x, y, button, clicks);
    mouse(KUI_C_MOUSE_RELEASED, x, y, button, clicks);
}

static void move_to(const char* id) {
    KuiRect rect = rect_of(id);
    mouse(KUI_C_MOUSE_MOVED, rect.x + rect.width / 2, rect.y + rect.height / 2,
          KUI_C_MOUSE_BUTTON_NONE, 0);
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

/* ---- Propagation: one listener per layer records where the event is ---------------------- */

static char g_trace[128];    /* current targets, in call order */
static char g_targets[128];  /* targets, in call order */
static int g_last_bubbles;   /* flags read by the last listener that ran */
static int g_last_cancelable;
static int g_last_prevented;

static void on_layer(KuiEvent* event, void* user_data) {
    KuiElement* current = NULL;
    char id[32];
    (void)user_data;
    current_id(event, id, sizeof id);
    append(g_trace, sizeof g_trace, id[0] ? id + 3 : "?"); /* "ev-inner" -> "inner" */
    target_id(event, id, sizeof id);
    append(g_targets, sizeof g_targets, id[0] ? id + 3 : "?");
    kui_event_get_flags(event, &g_last_bubbles, &g_last_cancelable, &g_last_prevented);
    if (kui_event_get_current_target(event, &current) == KUI_OK && current) {
        kui_element_class_list_add(current, "hit", 0);
        kui_element_release(current);
    }
}

/* "halt": the middle layer stops the event after its own listeners. */
static void on_halt(KuiEvent* event, void* user_data) {
    (void)user_data;
    kui_event_stop_propagation(event);
}

/* "veto": the inner layer cancels the default action. */
static void on_veto(KuiEvent* event, void* user_data) {
    (void)user_data;
    kui_event_prevent_default(event);
}

static int fire_layers(const char* id, const char* name, int bubbles, int cancelable) {
    static const char* const layers[] = {"ev-outer", "ev-middle", "ev-inner"};
    int not_canceled = -1;
    size_t index;
    g_trace[0] = '\0';
    g_targets[0] = '\0';
    g_last_bubbles = g_last_cancelable = g_last_prevented = -1;
    for (index = 0; index < 3; ++index) {
        KuiElement* layer = kuex_by_id(g_doc, layers[index]);
        if (layer) kui_element_class_list_remove(layer, "hit", 0);
        release(layer);
    }
    dispatch(id, name, bubbles, cancelable, &not_canceled);
    return not_canceled;
}

static void bind_layers(void) {
    static const char* const layers[] = {"ev-outer", "ev-middle", "ev-inner"};
    static const char* const names[] = {"ping", "halt", "veto"};
    size_t layer;
    size_t name;
    for (layer = 0; layer < 3; ++layer) {
        for (name = 0; name < 3; ++name) listen(layers[layer], names[name], on_layer, NULL);
    }
    listen("ev-middle", "halt", on_halt, NULL);
    listen("ev-inner", "veto", on_veto, NULL);
}

static void check_propagation(void) {
    int not_canceled;

    fire_layers("ev-inner", "ping", 1, 0);
    check("prop.bubble_order", strcmp(g_trace, "inner,middle,outer") == 0,
          "current targets '%s'", g_trace);
    check("prop.target", strcmp(g_targets, "inner,inner,inner") == 0, "targets '%s'", g_targets);

    fire_layers("ev-inner", "ping", 0, 0);
    check("prop.non_bubbling", strcmp(g_trace, "inner") == 0 && g_last_bubbles == 0,
          "current targets '%s', bubbles %d", g_trace, g_last_bubbles);

    fire_layers("ev-middle", "ping", 1, 0);
    check("prop.from_middle", strcmp(g_trace, "middle,outer") == 0 &&
          strcmp(g_targets, "middle,middle") == 0, "current targets '%s', targets '%s'", g_trace,
          g_targets);

    fire_layers("ev-inner", "halt", 1, 0);
    check("prop.stop_propagation", strcmp(g_trace, "inner,middle") == 0,
          "current targets '%s'", g_trace);

    not_canceled = fire_layers("ev-inner", "veto", 1, 1);
    check("cancel.prevent_default", not_canceled == 0 && g_last_prevented == 1 &&
          g_last_cancelable == 1 && g_last_bubbles == 1 && strcmp(g_trace, "inner,middle,outer") == 0,
          "not canceled %d, flags at outer b%d c%d p%d, trace '%s'", not_canceled, g_last_bubbles,
          g_last_cancelable, g_last_prevented, g_trace);

    /* prevent_default on a non-cancelable event changes nothing. */
    not_canceled = fire_layers("ev-inner", "veto", 1, 0);
    check("cancel.not_cancelable", not_canceled == 1 && g_last_prevented == 0 && g_last_cancelable == 0,
          "not canceled %d, flags at outer c%d p%d", not_canceled, g_last_cancelable,
          g_last_prevented);

    /* A detached element cannot be dispatched at (kui_c.h). */
    {
        KuiElement* loose = NULL;
        KuiStatus status = KUI_ERR_INTERNAL;
        if (kui_document_create_element(g_doc, "div", 0, &loose) == KUI_OK) {
            status = kui_element_dispatch_event(loose, "ping", 0, 1, 0, NULL);
        }
        release(loose);
        known("dispatch.detached", status == KUI_ERR_INVALID_ARG,
              "dispatch_event on an element that is not in the document returns KUI_OK",
              "status %d", status);
    }
}

/* ---- Listener management: ids, removing one, removing all of one name -------------------- */

static char g_tick_trace[128];
static KuiListenerId g_tick_ids[32];
static int g_tick_count;
static int g_tick_serial;

static void on_tick(KuiEvent* event, void* user_data) {
    char name[16];
    (void)event;
    snprintf(name, sizeof name, "%d", (int)(size_t)user_data);
    append(g_tick_trace, sizeof g_tick_trace, name);
}

static void on_tock(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    append(g_tick_trace, sizeof g_tick_trace, "tock");
}

static KuiListenerId add_tick(void) {
    KuiElement* target = kuex_by_id(g_doc, "lm-target");
    KuiListenerId id = 0;
    g_tick_serial++;
    if (target) {
        kui_element_add_event_listener(target, "tick", 0, on_tick, (void*)(size_t)g_tick_serial, &id);
    }
    release(target);
    return id;
}

static KuiStatus remove_tick(KuiListenerId id) {
    KuiElement* target = kuex_by_id(g_doc, "lm-target");
    KuiStatus status = KUI_ERR_NOT_FOUND;
    if (target) status = kui_element_remove_event_listener(target, "tick", 0, id);
    release(target);
    return status;
}

static KuiStatus remove_all(const char* name) {
    KuiElement* target = kuex_by_id(g_doc, "lm-target");
    KuiStatus status = KUI_ERR_NOT_FOUND;
    if (target) status = kui_element_remove_event_listeners(target, name, 0);
    release(target);
    return status;
}

static void fire_tick(const char* name) {
    g_tick_trace[0] = '\0';
    dispatch("lm-target", name, 0, 0, NULL);
}

static void check_listeners(void) {
    KuiListenerId first = add_tick();
    KuiListenerId second = add_tick();
    KuiListenerId third = add_tick();
    KuiStatus status;

    fire_tick("tick");
    check("listen.registration_order", strcmp(g_tick_trace, "1,2,3") == 0 && first && second &&
          third && first != second && second != third, "ran '%s', ids %llu %llu %llu",
          g_tick_trace, (unsigned long long)first, (unsigned long long)second,
          (unsigned long long)third);

    status = remove_tick(second);
    fire_tick("tick");
    check("listen.remove_event_listener", status == KUI_OK && strcmp(g_tick_trace, "1,3") == 0,
          "status %d, ran '%s'", status, g_tick_trace);

    listen("lm-target", "tock", on_tock, NULL);
    status = remove_all("tick");
    fire_tick("tick");
    check("listen.remove_event_listeners", status == KUI_OK && g_tick_trace[0] == '\0',
          "status %d, ran '%s'", status, g_tick_trace);
    fire_tick("tock");
    check("listen.remove_keeps_other_names", strcmp(g_tick_trace, "tock") == 0, "ran '%s'",
          g_tick_trace);
    remove_all("tock");
    g_tick_serial = 0;
}

/* ---- Event object: a synthetic event and the accessors of the other kinds ---------------- */

typedef struct {
    char type[32];
    KuiCEventTypeTag tag;
    int bubbles, cancelable, prevented;
    KuiStatus related, mouse_xy, mouse_button, key, code, modifiers, drag_count, drag_data;
    KuiStatus drag_files;
} ProbeResult;

static ProbeResult g_probe;

static void on_probe(KuiEvent* event, void* user_data) {
    KuiElement* related = NULL;
    float x, y;
    int value;
    size_t count;
    char buf[32];
    size_t length;
    (void)user_data;
    memset(&g_probe, 0, sizeof g_probe);
    type_of(event, g_probe.type, sizeof g_probe.type);
    g_probe.tag = (KuiCEventTypeTag)-1;
    kui_event_get_type_tag(event, &g_probe.tag);
    kui_event_get_flags(event, &g_probe.bubbles, &g_probe.cancelable, &g_probe.prevented);
    g_probe.related = kui_event_get_related_target(event, &related);
    release(related);
    g_probe.mouse_xy = kui_event_mouse_get_client_xy(event, &x, &y);
    g_probe.mouse_button = kui_event_mouse_get_button(event, &value);
    g_probe.key = kui_event_key_get_key(event, buf, sizeof buf, &length);
    g_probe.code = kui_event_key_get_code(event, buf, sizeof buf, &length);
    g_probe.modifiers = kui_event_key_get_modifiers(event, &value, NULL, NULL, NULL);
    g_probe.drag_count = kui_event_drag_get_type_count(event, &count);
    g_probe.drag_data = kui_event_drag_get_data(event, "text/plain", 0, buf, sizeof buf, &length);
    g_probe.drag_files = kui_event_drag_get_file_count(event, &count);
}

static void check_probe(void) {
    const ProbeResult* p = &g_probe;
    dispatch("probe", "probe", 1, 0, NULL);
    check("event.type", strcmp(p->type, "probe") == 0 && p->tag == KUI_C_EVENT_BASE,
          "type '%s', tag %d", p->type, (int)p->tag);
    check("event.flags", p->bubbles == 1 && p->cancelable == 0 && p->prevented == 0,
          "bubbles %d cancelable %d prevented %d", p->bubbles, p->cancelable, p->prevented);
    check("event.no_related_target", p->related == KUI_ERR_NOT_FOUND, "status %d", p->related);
    check("event.mouse_not_supported", p->mouse_xy == KUI_ERR_NOT_SUPPORTED &&
          p->mouse_button == KUI_ERR_NOT_SUPPORTED, "client %d, button %d", p->mouse_xy,
          p->mouse_button);
    check("event.key_not_supported", p->key == KUI_ERR_NOT_SUPPORTED &&
          p->code == KUI_ERR_NOT_SUPPORTED && p->modifiers == KUI_ERR_NOT_SUPPORTED,
          "key %d, code %d, modifiers %d", p->key, p->code, p->modifiers);
    check("event.drag_not_supported", p->drag_count == KUI_ERR_NOT_SUPPORTED &&
          p->drag_data == KUI_ERR_NOT_SUPPORTED && p->drag_files == KUI_ERR_NOT_SUPPORTED,
          "types %d, data %d, files %d", p->drag_count, p->drag_data, p->drag_files);
}

/* ---- Mouse: button, three coordinate spaces, related targets ----------------------------- */

typedef struct {
    int seen;
    int button;
    KuiCEventTypeTag tag;
    float cx, cy, ox, oy, px, py;
    int bubbles, cancelable, prevented;
} MouseRecord;

static MouseRecord g_click, g_down, g_dbl, g_ctx;

static void write_mouse_line(const char* type, const MouseRecord* r) {
    KuiElement* out = kuex_by_id(g_doc, "mouse-last");
    char text[160];
    char value[24];
    if (!out) return;
    snprintf(text, sizeof text, "%s button=%d client=%.0f,%.0f offset=%.0f,%.0f page=%.0f,%.0f",
             type, r->button, r->cx, r->cy, r->ox, r->oy, r->px, r->py);
    kui_element_set_text_content(out, text, 0);
    kui_element_set_attribute(out, "data-type", 0, type, 0);
    snprintf(value, sizeof value, "%d", r->button);
    kui_element_set_attribute(out, "data-button", 0, value, 0);
    snprintf(value, sizeof value, "%.2f", r->cx);
    kui_element_set_attribute(out, "data-cx", 0, value, 0);
    snprintf(value, sizeof value, "%.2f", r->cy);
    kui_element_set_attribute(out, "data-cy", 0, value, 0);
    snprintf(value, sizeof value, "%.2f", r->ox);
    kui_element_set_attribute(out, "data-ox", 0, value, 0);
    snprintf(value, sizeof value, "%.2f", r->oy);
    kui_element_set_attribute(out, "data-oy", 0, value, 0);
    snprintf(value, sizeof value, "%.2f", r->px);
    kui_element_set_attribute(out, "data-px", 0, value, 0);
    snprintf(value, sizeof value, "%.2f", r->py);
    kui_element_set_attribute(out, "data-py", 0, value, 0);
    kui_element_release(out);
}

static void on_mouse(KuiEvent* event, void* user_data) {
    MouseRecord* r = (MouseRecord*)user_data;
    char type[24];
    type_of(event, type, sizeof type);
    /* The pad keeps its own context menu: cancel the default one. */
    if (strcmp(type, "contextmenu") == 0) kui_event_prevent_default(event);
    r->seen++;
    r->button = -1;
    r->tag = (KuiCEventTypeTag)-1;
    kui_event_mouse_get_button(event, &r->button);
    kui_event_get_type_tag(event, &r->tag);
    kui_event_mouse_get_client_xy(event, &r->cx, &r->cy);
    kui_event_mouse_get_offset_xy(event, &r->ox, &r->oy);
    kui_event_mouse_get_page_xy(event, &r->px, &r->py);
    kui_event_get_flags(event, &r->bubbles, &r->cancelable, &r->prevented);
    if (strcmp(type, "mousedown") != 0) write_mouse_line(type, r);
}

static char g_enter[64];
static char g_leave[64];
static int g_enter_bubbles = -1;

static void on_hover(KuiEvent* event, void* user_data) {
    char type[24], current[32], related[32], text[160];
    int bubbles = -1;
    KuiElement* element = NULL;
    (void)user_data;
    type_of(event, type, sizeof type);
    current_id(event, current, sizeof current);
    related_id(event, related, sizeof related);
    kui_event_get_flags(event, &bubbles, NULL, NULL);
    if (strcmp(type, "mouseenter") == 0) {
        snprintf(g_enter, sizeof g_enter, "#%s from #%s", current, related[0] ? related : "-");
        g_enter_bubbles = bubbles;
    } else {
        snprintf(g_leave, sizeof g_leave, "#%s to #%s", current, related[0] ? related : "-");
    }
    if (kui_event_get_current_target(event, &element) == KUI_OK && element) {
        if (strcmp(type, "mouseenter") == 0) kui_element_class_list_add(element, "in", 0);
        else kui_element_class_list_remove(element, "in", 0);
        kui_element_release(element);
    }
    snprintf(text, sizeof text, "enter: %s | leave: %s", g_enter[0] ? g_enter : "-",
             g_leave[0] ? g_leave : "-");
    kuex_set_text(g_doc, "hover-out", text);
}

static void bind_mouse(void) {
    listen("mouse-pad", "click", on_mouse, &g_click);
    listen("mouse-pad", "mousedown", on_mouse, &g_down);
    listen("mouse-pad", "dblclick", on_mouse, &g_dbl);
    listen("mouse-pad", "contextmenu", on_mouse, &g_ctx);
    listen("hover-a", "mouseenter", on_hover, NULL);
    listen("hover-a", "mouseleave", on_hover, NULL);
    listen("hover-b", "mouseenter", on_hover, NULL);
    listen("hover-b", "mouseleave", on_hover, NULL);
}

/* ---- Wheel: the scroll amount of an injected wheel event -------------------------------------- */

static float g_wheel_dx = -1.0f, g_wheel_dy = -1.0f;
static int g_wheel_seen = 0;
static KuiStatus g_wheel_status = KUI_OK;
static KuiStatus g_probe_status = KUI_OK;

static void on_wheel(KuiEvent* event, void* user_data) {
    (void)user_data;
    ++g_wheel_seen;
    g_wheel_status = kui_event_wheel_get_delta(event, &g_wheel_dx, &g_wheel_dy);
}

static void on_wheel_probe(KuiEvent* event, void* user_data) {
    float x = 0, y = 0;
    (void)user_data;
    g_probe_status = kui_event_wheel_get_delta(event, &x, &y);
}

static void check_wheel(void) {
    KuiElement* pad = kuex_by_id(g_doc, "mouse-pad");
    KuiRect rect = rect_of("mouse-pad");
    KuiListenerId wheel_id = 0, probe_id = 0;
    int not_canceled = 0;
    float x = rect.x + rect.width / 2;
    float y = rect.y + rect.height / 2;

    if (!pad) {
        check("wheel.setup", 0, "missing #mouse-pad");
        return;
    }
    kui_element_add_event_listener(pad, "wheel", 0, on_wheel, NULL, &wheel_id);
    kui_devtools_dispatch_mouse_event(g_doc, KUI_C_MOUSE_WHEEL, x, y, KUI_C_MOUSE_BUTTON_NONE, 0, 0, 3.0f, -7.5f);
    kui_element_remove_event_listener(pad, "wheel", 0, wheel_id);
    check("wheel.delta", g_wheel_seen == 1 && g_wheel_status == KUI_OK && near(g_wheel_dx, 3.0f) && near(g_wheel_dy, -7.5f),
          "seen %d, status %d, delta %.1f,%.1f", g_wheel_seen, (int)g_wheel_status, g_wheel_dx, g_wheel_dy);

    /* Only a wheel event has a scroll amount. */
    kui_element_add_event_listener(pad, "wheel-probe", 0, on_wheel_probe, NULL, &probe_id);
    kui_element_dispatch_event(pad, "wheel-probe", 0, 0, 0, &not_canceled);
    kui_element_remove_event_listener(pad, "wheel-probe", 0, probe_id);
    check("wheel.delta_of_a_non_wheel_event", g_probe_status == KUI_ERR_NOT_SUPPORTED, "status %d", (int)g_probe_status);
    kui_element_release(pad);
}

/* Capture phase: listeners registered with capture != 0 run while the event travels down. */
static char g_capture_order[64];

static void on_capture_tag(KuiEvent* event, void* user_data) {
    size_t used = strlen(g_capture_order);
    (void)event;
    if (used + 4 < sizeof g_capture_order) {
        strcpy(g_capture_order + used, (const char*)user_data);
        strcat(g_capture_order, " ");
    }
}

static void check_capture(void) {
    KuiElement* outer = kuex_by_id(g_doc, "ev-outer");
    KuiElement* middle = kuex_by_id(g_doc, "ev-middle");
    KuiElement* inner = kuex_by_id(g_doc, "ev-inner");
    KuiListenerId outer_plain = 0, middle_plain = 0, inner_plain = 0, inner_capture = 0, outer_capture = 0;
    int not_canceled = 0;

    if (!(outer && middle && inner)) {
        check("capture.setup", 0, "missing the propagation layers");
        goto done;
    }
    kui_element_add_event_listener(outer, "capture-probe", 0, on_capture_tag, "op", &outer_plain);
    kui_element_add_event_listener(middle, "capture-probe", 0, on_capture_tag, "mp", &middle_plain);
    kui_element_add_event_listener(inner, "capture-probe", 0, on_capture_tag, "ip", &inner_plain);
    kui_element_add_event_listener_ex(inner, "capture-probe", 0, on_capture_tag, "ic", 1, &inner_capture);
    kui_element_add_event_listener_ex(outer, "capture-probe", 0, on_capture_tag, "oc", 1, &outer_capture);
    g_capture_order[0] = '\0';
    kui_element_dispatch_event(inner, "capture-probe", 0, 1, 0, &not_canceled);
    check("capture.order", strcmp(g_capture_order, "oc ic ip mp op ") == 0, "order '%s'", g_capture_order);

    kui_element_remove_event_listener(outer, "capture-probe", 0, outer_capture);
    g_capture_order[0] = '\0';
    kui_element_dispatch_event(inner, "capture-probe", 0, 1, 0, &not_canceled);
    check("capture.remove", strcmp(g_capture_order, "ic ip mp op ") == 0, "order '%s'", g_capture_order);

    kui_element_remove_event_listener(outer, "capture-probe", 0, outer_plain);
    kui_element_remove_event_listener(middle, "capture-probe", 0, middle_plain);
    kui_element_remove_event_listener(inner, "capture-probe", 0, inner_plain);
    kui_element_remove_event_listener(inner, "capture-probe", 0, inner_capture);
done:
    if (outer) kui_element_release(outer);
    if (middle) kui_element_release(middle);
    if (inner) kui_element_release(inner);
}

static void check_mouse(void) {
    KuiRect pad = rect_of("mouse-pad");
    float cx = pad.x + pad.width / 2;
    float cy = pad.y + pad.height / 2;
    float scroll_top = 0;
    KuiElement* root = NULL;

    kui_document_root(g_doc, &root);
    if (root) kui_element_get_scroll_metrics(root, NULL, &scroll_top, NULL, NULL);
    release(root);

    click_at("mouse-pad", KUI_C_MOUSE_BUTTON_LEFT, 1);
    check("mouse.click", g_click.seen == 1 && g_click.button == KUI_C_MOUSE_BUTTON_LEFT &&
          g_click.tag == KUI_C_EVENT_MOUSE && g_click.bubbles == 1 && g_click.cancelable == 1,
          "seen %d, button %d, tag %d, bubbles %d cancelable %d", g_click.seen, g_click.button,
          (int)g_click.tag, g_click.bubbles, g_click.cancelable);
    check("mouse.client_xy", near(g_click.cx, cx) && near(g_click.cy, cy),
          "client %.1f,%.1f, pad centre %.1f,%.1f", g_click.cx, g_click.cy, cx, cy);
    /* Offset coordinates are relative to the padding box: inside the 4px border. */
    known("mouse.offset_xy", near(g_click.ox, cx - pad.x - 4) && near(g_click.oy, cy - pad.y - 4),
          "mouse events never get offset coordinates (always 0,0)", "offset %.1f,%.1f, want %.1f,%.1f", g_click.ox, g_click.oy, cx - pad.x - 4, cy - pad.y - 4);
    known("mouse.page_xy", near(g_click.px, g_click.cx) && near(g_click.py, g_click.cy + scroll_top),
          "mouse events never get page coordinates (always 0,0)", "page %.1f,%.1f, client %.1f,%.1f, document scroll %.1f", g_click.px, g_click.py,
          g_click.cx, g_click.cy, scroll_top);

    click_at("mouse-pad", KUI_C_MOUSE_BUTTON_RIGHT, 1);
    check("mouse.right_button", g_down.button == KUI_C_MOUSE_BUTTON_RIGHT && g_ctx.seen == 1 &&
          g_ctx.button == KUI_C_MOUSE_BUTTON_RIGHT && g_ctx.prevented == 1,
          "mousedown button %d, contextmenu seen %d button %d prevented %d", g_down.button,
          g_ctx.seen, g_ctx.button, g_ctx.prevented);

    /* kui_c.h: click_count 2 injects the second click of a double-click. */
    click_at("mouse-pad", KUI_C_MOUSE_BUTTON_LEFT, 2);
    known("mouse.injected_click_count", g_dbl.seen == 1,
          "kui_devtools_dispatch_mouse_event ignores click_count (counts clicks by timing)",
          "dblclick seen %d", g_dbl.seen);
    /* Two clicks in a row on the same spot make a double-click. */
    g_dbl.seen = 0;
    click_at("mouse-pad", KUI_C_MOUSE_BUTTON_LEFT, 1);
    click_at("mouse-pad", KUI_C_MOUSE_BUTTON_LEFT, 2);
    check("mouse.dblclick", g_dbl.seen == 1 && g_dbl.button == KUI_C_MOUSE_BUTTON_LEFT,
          "dblclick seen %d button %d", g_dbl.seen, g_dbl.button);

    move_to("hover-a");
    move_to("hover-b");
    check("mouse.related_target", strcmp(g_leave, "#hover-a to #hover-b") == 0 &&
          strcmp(g_enter, "#hover-b from #hover-a") == 0, "leave '%s', enter '%s'", g_leave, g_enter);
    /* UI Events: mouseenter / mouseleave do not bubble. */
    known("mouse.enter_does_not_bubble", g_enter_bubbles == 0,
          "mouseenter / mouseleave are dispatched as bubbling events", "bubbles %d", g_enter_bubbles);
    move_to("probe");
}

/* ---- Keyboard and focus ------------------------------------------------------------------ */

typedef struct {
    int seen;
    char key[32];
    char code[32];
    int ctrl, shift, alt, meta;
    KuiCEventTypeTag tag;
    KuiStatus mouse;
} KeyRecord;

static KeyRecord g_key;
static char g_blur_line[64];
static char g_focus_line[64];
static KuiCEventTypeTag g_focus_tag = (KuiCEventTypeTag)-1;

static void on_key(KuiEvent* event, void* user_data) {
    char text[128];
    char mods[32] = "";
    float x, y;
    size_t length = 0;
    (void)user_data;
    memset(&g_key, 0, sizeof g_key);
    g_key.seen = 1;
    kui_event_key_get_key(event, g_key.key, sizeof g_key.key, &length);
    kui_event_key_get_code(event, g_key.code, sizeof g_key.code, &length);
    kui_event_key_get_modifiers(event, &g_key.ctrl, &g_key.shift, &g_key.alt, &g_key.meta);
    g_key.tag = (KuiCEventTypeTag)-1;
    kui_event_get_type_tag(event, &g_key.tag);
    g_key.mouse = kui_event_mouse_get_client_xy(event, &x, &y);
    if (g_key.ctrl) append(mods, sizeof mods, "ctrl");
    if (g_key.shift) append(mods, sizeof mods, "shift");
    if (g_key.alt) append(mods, sizeof mods, "alt");
    if (g_key.meta) append(mods, sizeof mods, "meta");
    snprintf(text, sizeof text, "keydown key=%s code=%s mods=%s", g_key.key, g_key.code,
             mods[0] ? mods : "none");
    kuex_set_text(g_doc, "key-out", text);
}

static void on_focus_change(KuiEvent* event, void* user_data) {
    char type[16], current[32], related[32];
    (void)user_data;
    type_of(event, type, sizeof type);
    current_id(event, current, sizeof current);
    related_id(event, related, sizeof related);
    kui_event_get_type_tag(event, &g_focus_tag);
    if (strcmp(type, "blur") == 0) {
        snprintf(g_blur_line, sizeof g_blur_line, "blur #%s to #%s", current, related[0] ? related : "-");
        kuex_set_text(g_doc, "focus-out", g_blur_line);
    } else {
        snprintf(g_focus_line, sizeof g_focus_line, "focus #%s from #%s", current,
                 related[0] ? related : "-");
        kuex_set_text(g_doc, "focus-out", g_focus_line);
    }
}

static void bind_keyboard(void) {
    listen("key-pad", "keydown", on_key, NULL);
    listen("key-pad", "focus", on_focus_change, NULL);
    listen("key-pad", "blur", on_focus_change, NULL);
    listen("key-other", "focus", on_focus_change, NULL);
}

static void check_keyboard(void) {
    focus_id("key-pad");
    /* Ctrl (2) + Shift (8), the DevTools modifier bits. */
    kui_devtools_dispatch_key_event(g_doc, KUI_C_KEY_DOWN, "a", 0, "KeyA", 0, 2 | 8, NULL, 0, 0);
    kui_devtools_dispatch_key_event(g_doc, KUI_C_KEY_UP, "a", 0, "KeyA", 0, 2 | 8, NULL, 0, 0);
    check("key.key_code", g_key.seen && strcmp(g_key.key, "a") == 0 && strcmp(g_key.code, "KeyA") == 0,
          "seen %d key '%s' code '%s'", g_key.seen, g_key.key, g_key.code);
    check("key.modifiers", g_key.ctrl == 1 && g_key.shift == 1 && g_key.alt == 0 && g_key.meta == 0,
          "ctrl %d shift %d alt %d meta %d", g_key.ctrl, g_key.shift, g_key.alt, g_key.meta);
    check("key.type_tag", g_key.tag == KUI_C_EVENT_KEYBOARD && g_key.mouse == KUI_ERR_NOT_SUPPORTED,
          "tag %d, mouse accessor status %d", (int)g_key.tag, g_key.mouse);

    focus_id("key-other");
    check("focus.blur_related_target", strcmp(g_blur_line, "blur #key-pad to #key-other") == 0,
          "'%s'", g_blur_line);
    check("focus.focus_related_target", strcmp(g_focus_line, "focus #key-other from #key-pad") == 0 &&
          g_focus_tag == KUI_C_EVENT_FOCUS, "'%s', tag %d", g_focus_line, (int)g_focus_tag);
    blur_id("key-other");
}

/* ---- Drag and drop: the data API from dragstart to dragend ------------------------------ */

typedef struct {
    char seq[160];           /* event types, repeats folded */
    char last[16];
    size_t start_types;      /* after set x3 + clear x1 in dragstart */
    char start_list[64];
    char start_read[32];
    KuiCEffectAllowed start_allowed;
    KuiCDropEffect over_effect;
    int drops;
    char drop_plain[32];
    char drop_custom[32];
    KuiStatus drop_junk;
    size_t drop_types;
    size_t drop_files;
    KuiStatus drop_file0;
    KuiCEffectAllowed drop_allowed;
    KuiCDropEffect drop_effect;
    KuiCEventTypeTag drop_tag;
    float drop_x, drop_y;
    size_t cleared_types;
    KuiCDropEffect end_effect;
} DragRecord;

static DragRecord g_drag;

static const char* effect_name(KuiCDropEffect effect) {
    switch (effect) {
        case KUI_C_DROP_COPY: return "copy";
        case KUI_C_DROP_MOVE: return "move";
        case KUI_C_DROP_LINK: return "link";
        default: return "none";
    }
}

static const char* allowed_name(KuiCEffectAllowed effect) {
    switch (effect) {
        case KUI_C_EFFECT_NONE: return "none";
        case KUI_C_EFFECT_COPY: return "copy";
        case KUI_C_EFFECT_MOVE: return "move";
        case KUI_C_EFFECT_LINK: return "link";
        case KUI_C_EFFECT_COPY_MOVE: return "copyMove";
        case KUI_C_EFFECT_COPY_LINK: return "copyLink";
        case KUI_C_EFFECT_MOVE_LINK: return "moveLink";
        case KUI_C_EFFECT_ALL: return "all";
        default: return "uninitialized";
    }
}

/* Types of the drag data joined with ','. */
static size_t drag_types(KuiEvent* event, char* out, size_t capacity) {
    size_t count = 0;
    size_t index;
    char type[48];
    size_t length;
    out[0] = '\0';
    kui_event_drag_get_type_count(event, &count);
    for (index = 0; index < count; ++index) {
        if (kui_event_drag_get_type(event, index, type, sizeof type, &length) == KUI_OK) {
            append(out, capacity, type);
        }
    }
    return count;
}

static void note_drag(const char* type) {
    if (strcmp(type, g_drag.last) == 0) return;
    snprintf(g_drag.last, sizeof g_drag.last, "%s", type);
    {
        size_t used = strlen(g_drag.seq);
        snprintf(g_drag.seq + used, sizeof g_drag.seq - used, "%s%s", used ? " > " : "", type);
    }
    kuex_set_text(g_doc, "drag-seq", g_drag.seq);
}

static void on_drag_source(KuiEvent* event, void* user_data) {
    char type[16];
    size_t length = 0;
    (void)user_data;
    type_of(event, type, sizeof type);
    if (strcmp(type, "dragstart") == 0) {
        memset(&g_drag, 0, sizeof g_drag);
        kuex_set_class(g_doc, "drop-zone", "");
        kuex_set_text(g_doc, "drop-out", "dragging...");
        kui_event_drag_set_effect_allowed(event, KUI_C_EFFECT_COPY_MOVE);
        kui_event_drag_set_data(event, "text/plain", 0, "kui-chip", 0);
        kui_event_drag_set_data(event, "text/x-kui", 0, "42", 0);
        kui_event_drag_set_data(event, "text/x-junk", 0, "scratch", 0);
        kui_event_drag_clear_data(event, "text/x-junk", 0);
        g_drag.start_types = drag_types(event, g_drag.start_list, sizeof g_drag.start_list);
        kui_event_drag_get_data(event, "text/plain", 0, g_drag.start_read, sizeof g_drag.start_read,
                                &length);
        g_drag.start_allowed = KUI_C_EFFECT_UNINITIALIZED;
        kui_event_drag_get_effect_allowed(event, &g_drag.start_allowed);
    } else if (strcmp(type, "dragend") == 0) {
        g_drag.end_effect = (KuiCDropEffect)-1;
        kui_event_drag_get_drop_effect(event, &g_drag.end_effect);
    }
    note_drag(type);
}

static void on_drop_zone(KuiEvent* event, void* user_data) {
    char type[16];
    char text[200];
    char files[96] = "";
    size_t length = 0;
    size_t index;
    (void)user_data;
    type_of(event, type, sizeof type);
    note_drag(type);
    if (strcmp(type, "dragenter") == 0 || strcmp(type, "dragover") == 0) {
        /* Accept the drop and ask for a copy. */
        kui_event_prevent_default(event);
        kui_event_drag_set_drop_effect(event, KUI_C_DROP_COPY);
        g_drag.over_effect = (KuiCDropEffect)-1;
        kui_event_drag_get_drop_effect(event, &g_drag.over_effect);
        kuex_set_class(g_doc, "drop-zone", "over");
        return;
    }
    if (strcmp(type, "dragleave") == 0) {
        if (!g_drag.drops) kuex_set_class(g_doc, "drop-zone", "");
        return;
    }
    /* drop */
    kui_event_prevent_default(event);
    g_drag.drops++;
    kui_event_drag_get_data(event, "text/plain", 0, g_drag.drop_plain, sizeof g_drag.drop_plain, &length);
    kui_event_drag_get_data(event, "text/x-kui", 0, g_drag.drop_custom, sizeof g_drag.drop_custom, &length);
    {
        char junk[16];
        g_drag.drop_junk = kui_event_drag_get_data(event, "text/x-junk", 0, junk, sizeof junk, &length);
    }
    kui_event_drag_get_type_count(event, &g_drag.drop_types);
    kui_event_drag_get_file_count(event, &g_drag.drop_files);
    for (index = 0; index < g_drag.drop_files; ++index) {
        char path[260];
        if (kui_event_drag_get_file(event, index, path, sizeof path, &length) == KUI_OK) {
            append(files, sizeof files, path);
        }
    }
    {
        char path[16];
        g_drag.drop_file0 = g_drag.drop_files ? KUI_OK :
            kui_event_drag_get_file(event, 0, path, sizeof path, &length);
    }
    kui_event_drag_get_effect_allowed(event, &g_drag.drop_allowed);
    kui_event_drag_get_drop_effect(event, &g_drag.drop_effect);
    kui_event_get_type_tag(event, &g_drag.drop_tag);
    kui_event_mouse_get_client_xy(event, &g_drag.drop_x, &g_drag.drop_y);
    /* Done with the data: clear every format. */
    kui_event_drag_clear_data(event, NULL, 0);
    kui_event_drag_get_type_count(event, &g_drag.cleared_types);

    snprintf(text, sizeof text, "dropped '%s' + '%s' | types %u | files %u%s%s | effect %s / allowed %s",
             g_drag.drop_plain, g_drag.drop_custom, (unsigned)g_drag.drop_types,
             (unsigned)g_drag.drop_files, files[0] ? ": " : "", files,
             effect_name(g_drag.drop_effect), allowed_name(g_drag.drop_allowed));
    kuex_set_text(g_doc, "drop-out", text);
    kuex_set_class(g_doc, "drop-zone", "dropped");
}

static void bind_drag(void) {
    listen("drag-src", "dragstart", on_drag_source, NULL);
    listen("drag-src", "dragend", on_drag_source, NULL);
    listen("drop-zone", "dragenter", on_drop_zone, NULL);
    listen("drop-zone", "dragover", on_drop_zone, NULL);
    listen("drop-zone", "dragleave", on_drop_zone, NULL);
    listen("drop-zone", "drop", on_drop_zone, NULL);
}

/* Press on the chip, move past the drag threshold, move onto the zone and release. */
static void simulate_drag(void) {
    KuiRect src = rect_of("drag-src");
    KuiRect zone = rect_of("drop-zone");
    float sx = src.x + src.width / 2;
    float sy = src.y + src.height / 2;
    float zx = zone.x + zone.width / 2;
    float zy = zone.y + zone.height / 2;
    mouse(KUI_C_MOUSE_MOVED, sx, sy, KUI_C_MOUSE_BUTTON_NONE, 0);
    mouse(KUI_C_MOUSE_PRESSED, sx, sy, KUI_C_MOUSE_BUTTON_LEFT, 1);
    mouse(KUI_C_MOUSE_MOVED, sx + 12, sy, KUI_C_MOUSE_BUTTON_LEFT, 0);
    mouse(KUI_C_MOUSE_MOVED, zx, zy, KUI_C_MOUSE_BUTTON_LEFT, 0);
    mouse(KUI_C_MOUSE_RELEASED, zx, zy, KUI_C_MOUSE_BUTTON_LEFT, 1);
}

static void check_drag(void) {
    KuiRect zone = rect_of("drop-zone");
    const DragRecord* d = &g_drag;
    simulate_drag();
    check("drag.sequence", strstr(d->seq, "dragstart") == d->seq && strstr(d->seq, "dragenter > dragover") &&
          strstr(d->seq, "drop") && strstr(d->seq, "dragend"), "sequence '%s'", d->seq);
    check("drag.set_clear_types", d->start_types == 2 && strcmp(d->start_list, "text/plain,text/x-kui") == 0 &&
          strcmp(d->start_read, "kui-chip") == 0, "types %u '%s', read back '%s'",
          (unsigned)d->start_types, d->start_list, d->start_read);
    check("drag.effect_allowed", d->start_allowed == KUI_C_EFFECT_COPY_MOVE &&
          d->drop_allowed == KUI_C_EFFECT_COPY_MOVE, "dragstart %d, drop %d", (int)d->start_allowed,
          (int)d->drop_allowed);
    check("drag.drop_effect", d->over_effect == KUI_C_DROP_COPY && d->drop_effect == KUI_C_DROP_COPY,
          "dragover %d, drop %d", (int)d->over_effect, (int)d->drop_effect);
    check("drag.get_data", d->drops == 1 && strcmp(d->drop_plain, "kui-chip") == 0 &&
          strcmp(d->drop_custom, "42") == 0 && d->drop_junk == KUI_ERR_NOT_FOUND && d->drop_types == 2,
          "drops %d, plain '%s', custom '%s', cleared format status %d, types %u", d->drops,
          d->drop_plain, d->drop_custom, d->drop_junk, (unsigned)d->drop_types);
    /* An in-page drag carries no files; index 0 is out of range. */
    check("drag.files", d->drop_files == 0 && d->drop_file0 == KUI_ERR_NOT_FOUND,
          "files %u, get_file(0) status %d", (unsigned)d->drop_files, d->drop_file0);
    check("drag.clear_all", d->cleared_types == 0, "types after clear %u", (unsigned)d->cleared_types);
    check("drag.drop_event", d->drop_tag == KUI_C_EVENT_DRAG && near(d->drop_x, zone.x + zone.width / 2) &&
          near(d->drop_y, zone.y + zone.height / 2), "tag %d, client %.1f,%.1f", (int)d->drop_tag,
          d->drop_x, d->drop_y);
    /* HTML DnD: dragend's dropEffect is the operation the drop target chose (copy). */
    known("drag.dragend_effect", d->end_effect == KUI_C_DROP_COPY,
          "dragend reports dropEffect 'move' whatever the target chose", "dragend effect %d",
          (int)d->end_effect);
    move_to("probe");
}

/* ---- Panel buttons ----------------------------------------------------------------------- */

static void show_prop(const char* name, int not_canceled) {
    char out[160];
    snprintf(out, sizeof out, "%s: %s | bubbles=%d cancelable=%d prevented=%d not_canceled=%d", name,
             g_trace[0] ? g_trace : "-", g_last_bubbles, g_last_cancelable, g_last_prevented,
             not_canceled);
    kuex_set_text(g_doc, "prop-out", out);
}

static void on_prop_bubble(KuiEvent* event, void* user_data) {
    int not_canceled;
    (void)event;
    (void)user_data;
    not_canceled = fire_layers("ev-inner", "ping", 1, 0);
    show_prop("ping", not_canceled);
}

static void on_prop_flat(KuiEvent* event, void* user_data) {
    int not_canceled;
    (void)event;
    (void)user_data;
    not_canceled = fire_layers("ev-inner", "ping", 0, 0);
    show_prop("ping (no bubbles)", not_canceled);
}

static void on_prop_stop(KuiEvent* event, void* user_data) {
    int not_canceled;
    (void)event;
    (void)user_data;
    not_canceled = fire_layers("ev-inner", "halt", 1, 0);
    show_prop("halt", not_canceled);
}

static void on_prop_cancel(KuiEvent* event, void* user_data) {
    int not_canceled;
    (void)event;
    (void)user_data;
    not_canceled = fire_layers("ev-inner", "veto", 1, 1);
    show_prop("veto", not_canceled);
}

static void show_listeners(const char* note) {
    char out[128];
    snprintf(out, sizeof out, "listeners=%d%s%s", g_tick_count, note[0] ? " | " : "", note);
    kuex_set_text(g_doc, "lm-out", out);
}

static void on_lm_add(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    if (g_tick_count < 32) g_tick_ids[g_tick_count++] = add_tick();
    show_listeners("");
}

static void on_lm_remove(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    if (g_tick_count > 0) remove_tick(g_tick_ids[--g_tick_count]);
    show_listeners("");
}

static void on_lm_clear(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    remove_all("tick");
    g_tick_count = 0;
    g_tick_serial = 0;
    show_listeners("");
}

static void on_lm_fire(KuiEvent* event, void* user_data) {
    char note[160];
    (void)event;
    (void)user_data;
    fire_tick("tick");
    snprintf(note, sizeof note, "ran: %s", g_tick_trace[0] ? g_tick_trace : "none");
    kuex_set_class(g_doc, "lm-target", g_tick_trace[0] ? "fired" : "");
    show_listeners(note);
}

static void on_probe_button(KuiEvent* event, void* user_data) {
    static const char* const tags[] = {"base", "mouse", "wheel", "pointer", "keyboard", "focus",
                                       "input", "drag"};
    const ProbeResult* p = &g_probe;
    char out[200];
    (void)event;
    (void)user_data;
    dispatch("probe", "probe", 1, 0, NULL);
    snprintf(out, sizeof out, "type=%s tag=%s bubbles=%d cancelable=%d | related=%s mouse=%s key=%s drag=%s",
             p->type, (unsigned)p->tag < 8 ? tags[p->tag] : "?", p->bubbles, p->cancelable,
             p->related == KUI_ERR_NOT_FOUND ? "none" : "?",
             p->mouse_xy == KUI_ERR_NOT_SUPPORTED ? "n/a" : "?",
             p->key == KUI_ERR_NOT_SUPPORTED ? "n/a" : "?",
             p->drag_count == KUI_ERR_NOT_SUPPORTED ? "n/a" : "?");
    kuex_set_text(g_doc, "probe-out", out);
}

/* The injected drag must not run inside this click's own dispatch: start it from a timer. */
static void run_simulated_drag(void* user_data) {
    (void)user_data;
    simulate_drag();
}

static void on_dnd_simulate(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    kuex_set_text(g_doc, "drag-seq", "-");
    kuex_set_text(g_doc, "drop-out", "-");
    kui_platform_set_timeout(run_simulated_drag, NULL, 10, NULL);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    int failed = 0;
    g_doc = host->document;

    bind_layers();
    listen("probe", "probe", on_probe, NULL);
    bind_mouse();
    bind_keyboard();
    bind_drag();

    check_propagation();
    check_listeners();
    check_probe();
    check_mouse();
    check_keyboard();
    check_drag();
    check_wheel();
    check_capture();
    finish();

    failed |= kuex_on(g_doc, "btn-prop-bubble", "click", on_prop_bubble, NULL);
    failed |= kuex_on(g_doc, "btn-prop-flat", "click", on_prop_flat, NULL);
    failed |= kuex_on(g_doc, "btn-prop-stop", "click", on_prop_stop, NULL);
    failed |= kuex_on(g_doc, "btn-prop-cancel", "click", on_prop_cancel, NULL);
    failed |= kuex_on(g_doc, "btn-lm-add", "click", on_lm_add, NULL);
    failed |= kuex_on(g_doc, "btn-lm-remove", "click", on_lm_remove, NULL);
    failed |= kuex_on(g_doc, "btn-lm-clear", "click", on_lm_clear, NULL);
    failed |= kuex_on(g_doc, "btn-lm-fire", "click", on_lm_fire, NULL);
    failed |= kuex_on(g_doc, "btn-probe", "click", on_probe_button, NULL);
    failed |= kuex_on(g_doc, "btn-dnd-simulate", "click", on_dnd_simulate, NULL);
    return failed;
}
