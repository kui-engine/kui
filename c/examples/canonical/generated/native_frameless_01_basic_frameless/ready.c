/* Copied by scripts/generate_c_examples.py from examples/c/ports/native_frameless_01_basic_frameless.c; edit that file, not this copy. */
/* Host logic of examples/native/frameless/01_basic_frameless/main.cpp: the custom title bar
 * buttons minimize, maximize / restore and close the frameless window. */
#include "kui_c_example_dom.h"

static KuiWindow* g_window;

static void on_minimize(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    kui_window_minimize(g_window);
}

static void on_maximize(KuiEvent* event, void* user_data) {
    int maximized = 0;
    (void)event;
    (void)user_data;
    kui_window_is_maximized(g_window, &maximized);
    if (maximized)
        kui_window_restore(g_window);
    else
        kui_window_maximize(g_window);
}

static void on_close(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    kui_window_close(g_window);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    KuiDocument* doc = host->document;
    g_window = host->window;
    return kuex_on(doc, "minBtn", "click", on_minimize, NULL) |
           kuex_on(doc, "maxBtn", "click", on_maximize, NULL) |
           kuex_on(doc, "closeBtn", "click", on_close, NULL);
}
