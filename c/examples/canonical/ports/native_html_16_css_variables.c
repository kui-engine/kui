/* Host logic of examples/native/html/16_css_variables/main.cpp: #themeBtn toggles the
 * theme-dark class on <body>, which redefines the CSS variables. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;
static int g_dark;

static void on_theme(KuiEvent* event, void* user_data) {
    KuiElement* body = NULL;
    (void)event;
    (void)user_data;
    g_dark = !g_dark;
    if (kui_document_body(g_doc, &body) != KUI_OK || !body) return;
    if (g_dark)
        kui_element_class_list_add(body, "theme-dark", 0);
    else
        kui_element_class_list_remove(body, "theme-dark", 0);
    kui_element_release(body);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    return kuex_on(g_doc, "themeBtn", "click", on_theme, NULL);
}
