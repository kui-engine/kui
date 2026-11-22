/* Host logic of examples/native/html/09_forms/main.cpp: the submit button reads the name input
 * and writes a greeting into the output box.
 *
 * The C++ original reads the input with getAttribute("value"), which is the content attribute
 * and stays empty while the user types, so it always greets "(empty)". This port reads the
 * live value with kui_element_get_value so the example does what it means to. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;

static void on_submit(KuiEvent* event, void* user_data) {
    char name[256];
    char message[320];
    (void)event;
    (void)user_data;
    kuex_get_value(g_doc, "nameInput", name, sizeof name);
    if (name[0] == '\0') snprintf(name, sizeof name, "(empty)");
    snprintf(message, sizeof message, "Hello, %s! Form submitted.", name);
    kuex_set_text(g_doc, "output", message);
    kuex_set_style(g_doc, "output", "background", "#dcfce7");
    kuex_set_style(g_doc, "output", "color", "#166534");
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    return kuex_on(g_doc, "submitBtn", "click", on_submit, NULL);
}
