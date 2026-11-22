/* Copied by scripts/generate_c_examples.py from examples/c/ports/native_component_09_tree_view.c; edit that file, not this copy. */
/* Host logic of examples/native/component/09_tree_view/main.cpp: clicking folder label fN toggles
 * "open" on its children list cN; clicking a file (i0..i5) writes "Selected: <file>". */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;

static void on_folder(KuiEvent* event, void* user_data) {
    char id[16];
    char class_name[64];
    (void)event;
    snprintf(id, sizeof id, "c%d", (int)(intptr_t)user_data);
    kuex_get_attr(g_doc, id, "class", class_name, sizeof class_name);
    kuex_set_class(g_doc, id, strstr(class_name, "open") ? "children" : "children open");
}

static void on_file(KuiEvent* event, void* user_data) {
    char text[128];
    char message[160];
    (void)user_data;
    kuex_target_text(event, text, sizeof text);
    snprintf(message, sizeof message, "Selected: %s", text);
    kuex_set_text(g_doc, "status", message);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    char id[16];
    int i;
    g_doc = host->document;
    for (i = 0; i < 3; ++i) {
        snprintf(id, sizeof id, "f%d", i);
        if (kuex_on(g_doc, id, "click", on_folder, (void*)(intptr_t)i) != 0) return 1;
    }
    for (i = 0; i < 6; ++i) {
        KuiElement* file;
        snprintf(id, sizeof id, "i%d", i);
        file = kuex_by_id(g_doc, id); /* the original skips missing files */
        if (!file) continue;
        kui_element_release(file);
        if (kuex_on(g_doc, id, "click", on_file, NULL) != 0) return 1;
    }
    return 0;
}
