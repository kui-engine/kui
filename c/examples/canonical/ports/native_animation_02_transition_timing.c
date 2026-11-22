/* Host logic of examples/native/animation/02_transition_timing/main.cpp: #goBtn flips the six
 * dots #d1..#d6 between left: calc(100% - 20px) and left: 0, so each runs its own transition. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;
static int g_at_end;

static void on_go(KuiEvent* event, void* user_data) {
    char id[8];
    int i;
    (void)event;
    (void)user_data;
    g_at_end = !g_at_end;
    for (i = 1; i <= 6; ++i) {
        snprintf(id, sizeof id, "d%d", i);
        kuex_set_style(g_doc, id, "left", g_at_end ? "calc(100% - 20px)" : "0");
    }
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    return kuex_on(g_doc, "goBtn", "click", on_go, NULL);
}
