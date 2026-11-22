/* Host logic of examples/native/showcase/02_chat_app/main.cpp: Send appends the typed text to
 * #msgs as a "msg sent" bubble and clears #msgInput; an empty input sends nothing.
 *
 * The C++ original reads and clears the input through the "value" content attribute, which
 * stays empty while the user types, so it never sends anything. This port reads and clears the
 * live value with kui_element_get_value / kui_element_set_value. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;

static void on_send(KuiEvent* event, void* user_data) {
    char text[1024];
    KuiElement* msg = NULL;
    KuiElement* list;
    (void)event;
    (void)user_data;
    kuex_get_value(g_doc, "msgInput", text, sizeof text);
    if (text[0] == '\0') return;
    if (kui_document_create_element(g_doc, "div", 0, &msg) != KUI_OK) return;
    kui_element_set_class_name(msg, "msg sent", 0);
    kui_element_set_text_content(msg, text, 0);
    list = kuex_by_id(g_doc, "msgs");
    if (list) {
        kui_element_append_child(list, msg);
        kui_element_release(list);
    }
    kui_element_release(msg);
    kuex_set_value(g_doc, "msgInput", "");
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    return kuex_on(g_doc, "sendBtn", "click", on_send, NULL);
}
