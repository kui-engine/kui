/* Host logic of examples/native/advanced/01_dom_manipulation/main.cpp: Add appends an "Item #n"
 * card to the container, Remove Last drops the last card, Clear All empties it; the counter
 * always shows the container's element count. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;
static int g_next_id = 1;

static void update_counter(KuiElement* container) {
    size_t count = 0;
    char line[64];
    kui_element_child_count(container, &count);
    snprintf(line, sizeof line, "Elements: %zu", count);
    kuex_set_text(g_doc, "counter", line);
}

static void on_add(KuiEvent* event, void* user_data) {
    KuiElement* container = kuex_by_id(g_doc, "container");
    KuiElement* card = NULL;
    char text[64];
    (void)event;
    (void)user_data;
    if (!container) return;
    if (kui_document_create_element(g_doc, "div", 0, &card) == KUI_OK) {
        kui_element_set_class_name(card, "card", 0);
        snprintf(text, sizeof text, "Item #%d", g_next_id++);
        kui_element_set_text_content(card, text, 0);
        kui_element_append_child(container, card);
        kui_element_release(card);
        update_counter(container);
    }
    kui_element_release(container);
}

static void on_remove(KuiEvent* event, void* user_data) {
    KuiElement* container = kuex_by_id(g_doc, "container");
    KuiElement* last = NULL;
    (void)event;
    (void)user_data;
    if (!container) return;
    if (kui_element_last_child(container, &last) == KUI_OK && last) {
        kui_element_remove_child(container, last);
        kui_element_release(last);
        update_counter(container);
    }
    kui_element_release(container);
}

static void on_clear(KuiEvent* event, void* user_data) {
    KuiElement* container = kuex_by_id(g_doc, "container");
    (void)event;
    (void)user_data;
    if (!container) return;
    kui_element_set_inner_html(container, "", 0);
    update_counter(container);
    kui_element_release(container);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    return kuex_on(g_doc, "addBtn", "click", on_add, NULL) |
           kuex_on(g_doc, "removeBtn", "click", on_remove, NULL) |
           kuex_on(g_doc, "clearBtn", "click", on_clear, NULL);
}
