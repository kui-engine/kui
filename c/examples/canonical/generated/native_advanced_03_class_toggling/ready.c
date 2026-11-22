/* Copied by scripts/generate_c_examples.py from examples/c/ports/native_advanced_03_class_toggling.c; edit that file, not this copy. */
/* Host logic of examples/native/advanced/03_class_toggling/main.cpp: three buttons toggle the
 * expanded / highlight / danger classes on the panel and echo its class list. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;

static void toggle(const char* class_name) {
    char classes[256] = "";
    char line[300];
    size_t length = 0;
    KuiElement* panel = kuex_by_id(g_doc, "panel");
    if (!panel) return;
    kui_element_class_list_toggle(panel, class_name, 0);
    if (kui_element_get_class_name(panel, classes, sizeof classes, &length) != KUI_OK) classes[0] = '\0';
    classes[sizeof classes - 1] = '\0';
    kui_element_release(panel);
    snprintf(line, sizeof line, "Classes: %s", classes);
    kuex_set_text(g_doc, "classes", line);
}

static void on_expand(KuiEvent* event, void* user_data) { (void)event; (void)user_data; toggle("expanded"); }
static void on_highlight(KuiEvent* event, void* user_data) { (void)event; (void)user_data; toggle("highlight"); }
static void on_danger(KuiEvent* event, void* user_data) { (void)event; (void)user_data; toggle("danger"); }

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    return kuex_on(g_doc, "toggleExpand", "click", on_expand, NULL) |
           kuex_on(g_doc, "toggleHighlight", "click", on_highlight, NULL) |
           kuex_on(g_doc, "toggleDanger", "click", on_danger, NULL);
}
