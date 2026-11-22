/* Copied by scripts/generate_c_examples.py from examples/c/ports/kui_hello_world.c; edit that file, not this copy. */
/* Host logic of examples/hello_world/main.cpp: #clickBtn increments #counter; the counter turns
 * #f59e0b from 5 clicks and #10b981 from 10. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;
static int g_count;

static void on_click(KuiEvent* event, void* user_data) {
    char text[16];
    (void)event;
    (void)user_data;
    ++g_count;
    snprintf(text, sizeof text, "%d", g_count);
    kuex_set_text(g_doc, "counter", text);
    if (g_count >= 10)
        kuex_set_style(g_doc, "counter", "color", "#10b981");
    else if (g_count >= 5)
        kuex_set_style(g_doc, "counter", "color", "#f59e0b");
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    return kuex_on(g_doc, "clickBtn", "click", on_click, NULL);
}
