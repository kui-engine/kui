/* Copied by scripts/generate_c_examples.py from examples/c/ports/kui_scrollbar_overlay_demo.c; edit that file, not this copy. */
/* Host logic of examples/scrollbar_overlay_demo/main.cpp: #toggle-target starts with
 * -kui-scrollbar-overlay: overlay and #toggle-btn flips it between overlay and auto (updating
 * #toggle-status); #btn-top / #btn-bottom scrollTo the top / bottom of #scrollzone.
 *
 * #btn-fling starts an inertial fling (kui_element_start_momentum_scroll(0, 1.5), px/ms downward).
 * The original's registerBuiltinComponents runs before loadHTML and the page uses none, so it
 * is not repeated here. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;
static int g_overlay_on = 1;

static void on_toggle(KuiEvent* event, void* user_data) {
    const char* mode;
    char status[128];
    (void)event;
    (void)user_data;
    g_overlay_on = !g_overlay_on;
    mode = g_overlay_on ? "overlay" : "auto";
    kuex_set_style(g_doc, "toggle-target", "-kui-scrollbar-overlay", mode);
    snprintf(status, sizeof status, "current: -kui-scrollbar-overlay: %s%s", mode,
             g_overlay_on ? "  (scrollbar overlays content)" : "  (scrollbar reserves 8px)");
    kuex_set_text(g_doc, "toggle-status", status);
}

static void scroll_zone(int to_bottom) {
    KuiElement* zone = kuex_by_id(g_doc, "scrollzone");
    float left = 0, top = 0, width = 0, height = 0;
    if (!zone) return;
    kui_element_get_scroll_metrics(zone, &left, &top, &width, &height);
    /* scroll-behavior:smooth on .scrollzone makes this animate. */
    kui_element_set_scroll_position(zone, left, to_bottom ? height : 0.0f);
    kui_element_release(zone);
}

static void on_top(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    scroll_zone(0);
}

static void on_bottom(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    scroll_zone(1);
}

static void on_fling(KuiEvent* event, void* user_data) {
    KuiElement* zone = kuex_by_id(g_doc, "scrollzone");
    (void)event;
    (void)user_data;
    if (!zone) return;
    kui_element_start_momentum_scroll(zone, 0.0f, 1.5f); /* px/ms downward inertial fling */
    kui_element_release(zone);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    KuiElement* target;
    KuiElement* zone;
    g_doc = host->document;

    target = kuex_by_id(g_doc, "toggle-target");
    if (target) {
        kui_element_set_style_property(target, "-kui-scrollbar-overlay", 0, "overlay", 0, 0);
        kui_element_release(target);
        kuex_on(g_doc, "toggle-btn", "click", on_toggle, NULL);
    } else {
        fprintf(stderr, "[scrollbar_overlay_demo] WARN: toggle elements missing; "
                        "runtime switch disabled\n");
    }

    zone = kuex_by_id(g_doc, "scrollzone");
    if (zone) {
        kui_element_release(zone);
        kuex_on(g_doc, "btn-top", "click", on_top, NULL);
        kuex_on(g_doc, "btn-bottom", "click", on_bottom, NULL);
        kuex_on(g_doc, "btn-fling", "click", on_fling, NULL);
    }
    return 0;
}
