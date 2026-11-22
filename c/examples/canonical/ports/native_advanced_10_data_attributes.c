/* Host logic of examples/native/advanced/10_data_attributes/main.cpp: the Run Demo button writes
 * a success message into the output box and turns it green. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;

static void on_action(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    kuex_set_text(g_doc, "output", "Data Attributes demo executed successfully!");
    kuex_set_style(g_doc, "output", "background", "#dcfce7");
    kuex_set_style(g_doc, "output", "color", "#166534");
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    return kuex_on(g_doc, "actionBtn", "click", on_action, NULL);
}
