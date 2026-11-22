/* Copied by scripts/generate_c_examples.py from examples/c/ports/native_component_10_list_view.c; edit that file, not this copy. */
/* Host logic of examples/native/component/10_list_view/main.cpp: clicking row N selects it (only
 * that row gets "li sel") and writes "Selected row N+1" into the status bar. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;

static void on_row(KuiEvent* event, void* user_data) {
    int index = (int)(intptr_t)user_data;
    char id[16];
    char message[32];
    int j;
    (void)event;
    for (j = 0; j < 5; ++j) {
        snprintf(id, sizeof id, "r%d", j);
        kuex_set_class(g_doc, id, "li");
    }
    snprintf(id, sizeof id, "r%d", index);
    kuex_set_class(g_doc, id, "li sel");
    snprintf(message, sizeof message, "Selected row %d", index + 1);
    kuex_set_text(g_doc, "status", message);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    char id[16];
    int i;
    g_doc = host->document;
    for (i = 0; i < 5; ++i) {
        snprintf(id, sizeof id, "r%d", i);
        if (kuex_on(g_doc, id, "click", on_row, (void*)(intptr_t)i) != 0) return 1;
    }
    return 0;
}
