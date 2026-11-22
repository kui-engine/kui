/* Copied by scripts/generate_c_examples.py from examples/c/ports/native_platform_06_timer_intervals.c; edit that file, not this copy. */
/* Host logic of examples/native/platform/06_timer_intervals/main.cpp: Start runs a 100 ms interval
 * that bumps #counter and sets #bar's width to count % 100 (ignored while running); Stop clears
 * it; Reset clears it and goes back to 0. Each action writes a line into #log. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;
static int g_count;
static KuiTimerId g_timer;

static void update_ui(void) {
    char text[32];
    snprintf(text, sizeof text, "%d", g_count);
    kuex_set_text(g_doc, "counter", text);
    snprintf(text, sizeof text, "%d%%", g_count % 100);
    kuex_set_style(g_doc, "bar", "width", text);
}

static void on_tick(void* user_data) {
    (void)user_data;
    ++g_count;
    update_ui();
}

static void on_start(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    if (g_timer) return;
    kuex_set_text(g_doc, "log", "Timer started");
    if (kui_platform_set_interval(on_tick, NULL, 100, &g_timer) != KUI_OK) g_timer = 0;
}

static void on_stop(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    if (!g_timer) return;
    kui_platform_clear_timer(g_timer);
    g_timer = 0;
    kuex_set_text(g_doc, "log", "Timer stopped");
}

static void on_reset(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    if (g_timer) {
        kui_platform_clear_timer(g_timer);
        g_timer = 0;
    }
    g_count = 0;
    update_ui();
    kuex_set_text(g_doc, "log", "Timer reset");
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    return kuex_on(g_doc, "startBtn", "click", on_start, NULL) |
           kuex_on(g_doc, "stopBtn", "click", on_stop, NULL) |
           kuex_on(g_doc, "resetBtn", "click", on_reset, NULL);
}
