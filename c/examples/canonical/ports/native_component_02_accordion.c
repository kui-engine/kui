/* Host logic of examples/native/component/02_accordion/main.cpp: clicking header N closes every
 * section and reopens section N unless it was already open. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;

static void on_header(KuiEvent* event, void* user_data) {
    int index = (int)(intptr_t)user_data;
    char id[16];
    char class_name[64];
    int was_open;
    int i;
    (void)event;
    snprintf(id, sizeof id, "h%d", index);
    kuex_get_attr(g_doc, id, "class", class_name, sizeof class_name);
    was_open = strstr(class_name, "open") != NULL;
    for (i = 0; i < 4; ++i) {
        snprintf(id, sizeof id, "h%d", i);
        kuex_set_class(g_doc, id, "ah");
        snprintf(id, sizeof id, "b%d", i);
        kuex_set_class(g_doc, id, "ab");
    }
    if (!was_open) {
        snprintf(id, sizeof id, "h%d", index);
        kuex_set_class(g_doc, id, "ah open");
        snprintf(id, sizeof id, "b%d", index);
        kuex_set_class(g_doc, id, "ab open");
    }
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    char id[16];
    int i;
    g_doc = host->document;
    for (i = 0; i < 4; ++i) {
        snprintf(id, sizeof id, "h%d", i);
        if (kuex_on(g_doc, id, "click", on_header, (void*)(intptr_t)i) != 0) return 1;
    }
    return 0;
}
