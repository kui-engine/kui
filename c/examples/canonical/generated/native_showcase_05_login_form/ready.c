/* Copied by scripts/generate_c_examples.py from examples/c/ports/native_showcase_05_login_form.c; edit that file, not this copy. */
/* Host logic of examples/native/showcase/05_login_form/main.cpp: Sign In shows an error in #msg
 * when the email or password is empty, otherwise a welcome for the email.
 *
 * The C++ original reads both inputs with getAttribute("value"), which is the content attribute
 * and stays empty while the user types, so it always reports the error. This port reads the
 * live values with kui_element_get_value. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;

static void on_login(KuiEvent* event, void* user_data) {
    char email[256];
    char pass[256];
    char message[320];
    (void)event;
    (void)user_data;
    kuex_get_value(g_doc, "email", email, sizeof email);
    kuex_get_value(g_doc, "pass", pass, sizeof pass);
    if (email[0] == '\0' || pass[0] == '\0') {
        kuex_set_class(g_doc, "msg", "msg msg-err show");
        kuex_set_text(g_doc, "msg", "Please fill in all fields.");
    } else {
        snprintf(message, sizeof message, "Login successful! Welcome, %s", email);
        kuex_set_class(g_doc, "msg", "msg msg-ok show");
        kuex_set_text(g_doc, "msg", message);
    }
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    return kuex_on(g_doc, "loginBtn", "click", on_login, NULL);
}
