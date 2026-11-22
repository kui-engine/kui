/* Host logic of examples/native/component/05_breadcrumb/main.cpp: clicking any breadcrumb link
 * (bc0..bc6) writes "Navigated to: <link text>" into the status line. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;

static void on_link(KuiEvent* event, void* user_data) {
    char text[128];
    char message[160];
    (void)user_data;
    kuex_target_text(event, text, sizeof text);
    snprintf(message, sizeof message, "Navigated to: %s", text);
    kuex_set_text(g_doc, "status", message);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    char id[16];
    int i;
    g_doc = host->document;
    for (i = 0; i < 7; ++i) {
        KuiElement* link;
        snprintf(id, sizeof id, "bc%d", i);
        link = kuex_by_id(g_doc, id); /* the original skips missing links */
        if (!link) continue;
        kui_element_release(link);
        if (kuex_on(g_doc, id, "click", on_link, NULL) != 0) return 1;
    }
    return 0;
}
