/* Host logic of examples/native/component/01_tabs/main.cpp: clicking tab N marks tab N and
 * panel N active and every other tab / panel inactive. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;

static void on_tab(KuiEvent* event, void* user_data) {
    int index = (int)(intptr_t)user_data;
    char id[16];
    int i;
    (void)event;
    for (i = 0; i < 4; ++i) {
        snprintf(id, sizeof id, "tab%d", i);
        kuex_set_class(g_doc, id, i == index ? "tab active" : "tab");
        snprintf(id, sizeof id, "p%d", i);
        kuex_set_class(g_doc, id, i == index ? "panel active" : "panel");
    }
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    char id[16];
    int i;
    g_doc = host->document;
    for (i = 0; i < 4; ++i) {
        snprintf(id, sizeof id, "tab%d", i);
        if (kuex_on(g_doc, id, "click", on_tab, (void*)(intptr_t)i) != 0) return 1;
    }
    return 0;
}
