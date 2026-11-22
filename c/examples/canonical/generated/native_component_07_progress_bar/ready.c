/* Copied by scripts/generate_c_examples.py from examples/c/ports/native_component_07_progress_bar.c; edit that file, not this copy. */
/* Host logic of examples/native/component/07_progress_bar/main.cpp: Start runs a 50 ms interval
 * that grows #pf1 / #lbl1 by 2% up to 100% (ignored while one is running); Reset stops it and
 * goes back to 0%. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;
static int g_progress;
static KuiTimerId g_timer;

static void show_progress(void) {
    char value[16];
    snprintf(value, sizeof value, "%d%%", g_progress);
    kuex_set_style(g_doc, "pf1", "width", value);
    kuex_set_text(g_doc, "lbl1", value);
}

static void on_tick(void* user_data) {
    (void)user_data;
    g_progress += 2;
    if (g_progress > 100) g_progress = 100;
    show_progress();
    if (g_progress >= 100) {
        kui_platform_clear_timer(g_timer);
        g_timer = 0;
    }
}

static void on_start(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    if (g_timer) return;
    g_progress = 0;
    if (kui_platform_set_interval(on_tick, NULL, 50, &g_timer) != KUI_OK) g_timer = 0;
}

static void on_reset(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    if (g_timer) {
        kui_platform_clear_timer(g_timer);
        g_timer = 0;
    }
    g_progress = 0;
    show_progress();
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    return kuex_on(g_doc, "startBtn", "click", on_start, NULL) |
           kuex_on(g_doc, "resetBtn", "click", on_reset, NULL);
}
