/* Copied by scripts/generate_c_examples.py from examples/c/ports/native_component_08_tag_input.c; edit that file, not this copy. */
/* Host logic of examples/native/component/08_tag_input/main.cpp: clicking a suggestion (s0..s5)
 * appends a new <div class="tag"> with the suggestion's text to #tags. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;

static void on_suggestion(KuiEvent* event, void* user_data) {
    char text[128];
    KuiElement* container;
    KuiElement* tag = NULL;
    (void)user_data;
    kuex_target_text(event, text, sizeof text);
    container = kuex_by_id(g_doc, "tags");
    if (!container) return;
    if (kui_document_create_element(g_doc, "div", 0, &tag) == KUI_OK && tag) {
        kui_element_set_class_name(tag, "tag", 0);
        kui_element_set_text_content(tag, text, 0);
        kui_element_append_child(container, tag);
        kui_element_release(tag);
    }
    kui_element_release(container);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    char id[16];
    int i;
    g_doc = host->document;
    for (i = 0; i < 6; ++i) {
        snprintf(id, sizeof id, "s%d", i);
        if (kuex_on(g_doc, id, "click", on_suggestion, NULL) != 0) return 1;
    }
    return 0;
}
