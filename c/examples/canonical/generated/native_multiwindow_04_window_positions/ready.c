/* Copied by scripts/generate_c_examples.py from examples/c/ports/native_multiwindow_04_window_positions.c; edit that file, not this copy. */
/* Host logic of examples/native/multiwindow/04_window_positions/main.cpp: moves the window to the
 * primary monitor's work area + (48, 48), then a 200 ms interval writes the live OS position
 * into #position. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;
static KuiWindow* g_window;

static void on_tick(void* user_data) {
    char text[96];
    int x = 0, y = 0;
    (void)user_data;
    kui_window_get_position(g_window, &x, &y);
    snprintf(text, sizeof text, "Live window position: (%d, %d)", x, y);
    kuex_set_text(g_doc, "position", text);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    KuiCMonitorInfo monitor;
    KuiTimerId timer = 0;
    g_doc = host->document;
    g_window = host->window;
    memset(&monitor, 0, sizeof monitor);
    kui_platform_get_primary_monitor(&monitor);
    kui_window_set_position(g_window, (int)(monitor.work_x + 48), (int)(monitor.work_y + 48));
    return kui_platform_set_interval(on_tick, NULL, 200, &timer) == KUI_OK ? 0 : 1;
}
