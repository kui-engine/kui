/* Host logic of examples/native/platform/07_window_events/main.cpp: window focus / blur /
 * focusin / focusout / movestart / move / moveend / dragregionpointerdown / close listeners
 * update the #b-focus / #b-move badges and prepend lines to #log; the custom title-bar buttons
 * minimize and close the (frameless) window. */
#include "kui_c_example_dom.h"

#include <stdlib.h>

static KuiDocument* g_doc;
static KuiWindow* g_window;

/* Newest line first: #log = line + "\n" + #log. */
static void log_line(const char* line) {
    KuiElement* log = kuex_by_id(g_doc, "log");
    size_t length = 0;
    size_t line_length = strlen(line);
    char* joined;
    if (!log) return;
    kui_element_get_text_content(log, NULL, 0, &length);
    joined = (char*)malloc(line_length + 1 + length + 1);
    if (joined) {
        memcpy(joined, line, line_length);
        joined[line_length] = '\n';
        kui_element_get_text_content(log, joined + line_length + 1, length + 1, &length);
        kui_element_set_text_content(log, joined, 0);
        free(joined);
    }
    kui_element_release(log);
}

static void set_badge(const char* id, const char* text, int add_active, int remove_active) {
    KuiElement* badge = kuex_by_id(g_doc, id);
    if (!badge) return;
    kui_element_set_text_content(badge, text, 0);
    if (remove_active) kui_element_class_list_remove(badge, "active", 0);
    if (add_active) kui_element_class_list_add(badge, "active", 0);
    kui_element_release(badge);
}

static void on_focus(KuiEvent* e, void* u) {
    (void)e; (void)u;
    set_badge("b-focus", "app: focus", 1, 1);
    log_line("[focus]  application activated");
}
static void on_blur(KuiEvent* e, void* u) {
    (void)e; (void)u;
    set_badge("b-focus", "app: blur", 0, 1);
    log_line("[blur]   application deactivated");
}
static void on_focusin(KuiEvent* e, void* u) {
    (void)e; (void)u;
    log_line("[focusin]  keyboard focus gained");
}
static void on_focusout(KuiEvent* e, void* u) {
    (void)e; (void)u;
    log_line("[focusout] keyboard focus lost");
}
static void on_movestart(KuiEvent* e, void* u) {
    (void)e; (void)u;
    kuex_set_text(g_doc, "b-move", "move: dragging");
    log_line("[movestart] window drag begin (passed OS drag threshold)");
}
static void on_move(KuiEvent* e, void* u) {
    (void)e; (void)u;
    kuex_set_text(g_doc, "b-move", "move: moving");
}
static void on_moveend(KuiEvent* e, void* u) {
    (void)e; (void)u;
    kuex_set_text(g_doc, "b-move", "move: idle");
    log_line("[moveend]  window drag end");
}
static void on_drag_region(KuiEvent* e, void* u) {
    (void)e; (void)u;
    log_line("[dragregionpointerdown] title-bar pressed");
}
static void on_close(KuiEvent* e, void* u) {
    (void)e; (void)u;
    log_line("[close]  close requested (allowed)");
}
static void on_minimize_click(KuiEvent* e, void* u) {
    (void)e; (void)u;
    kui_window_minimize(g_window);
}
static void on_close_click(KuiEvent* e, void* u) {
    (void)e; (void)u;
    kui_window_close(g_window);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    static const struct { const char* name; KuiWindowEventFn fn; } events[] = {
        {"focus", on_focus}, {"blur", on_blur}, {"focusin", on_focusin},
        {"focusout", on_focusout}, {"movestart", on_movestart}, {"move", on_move},
        {"moveend", on_moveend}, {"dragregionpointerdown", on_drag_region}, {"close", on_close},
    };
    size_t i;
    int failed = 0;
    g_doc = host->document;
    g_window = host->window;
    for (i = 0; i < sizeof events / sizeof events[0]; ++i) {
        if (kui_window_add_event_listener(g_window, events[i].name, 0, events[i].fn, NULL, 0,
                                          NULL) != KUI_OK) {
            failed = 1;
        }
    }
    failed |= kuex_on(g_doc, "btn-min", "click", on_minimize_click, NULL);
    failed |= kuex_on(g_doc, "btn-close", "click", on_close_click, NULL);
    log_line("ready — try dragging the title bar / switching windows");
    return failed;
}
