/* Copied by scripts/generate_c_examples.py from examples/c/ports/native_showcase_04_settings_page.c; edit that file, not this copy. */
/* Host logic of examples/native/showcase/04_settings_page/main.cpp: clicking a switch #t0..#t2
 * flips its class between "toggle on" and "toggle". */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;
static const char* const k_ids[] = {"t0", "t1", "t2"};

static void on_toggle(KuiEvent* event, void* user_data) {
    const char* id = (const char*)user_data;
    char class_name[64];
    size_t length = 0;
    KuiElement* element = kuex_by_id(g_doc, id);
    (void)event;
    if (!element) return;
    class_name[0] = '\0';
    kui_element_get_class_name(element, class_name, sizeof class_name, &length);
    class_name[sizeof class_name - 1] = '\0';
    /* Same test as the original: className().find("on"). */
    kui_element_set_class_name(element, strstr(class_name, "on") ? "toggle" : "toggle on", 0);
    kui_element_release(element);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    int failed = 0;
    int i;
    g_doc = host->document;
    for (i = 0; i < 3; ++i) failed |= kuex_on(g_doc, k_ids[i], "click", on_toggle, (void*)k_ids[i]);
    return failed;
}
