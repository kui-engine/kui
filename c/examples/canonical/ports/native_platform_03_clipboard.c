/* Host logic of examples/native/platform/03_clipboard/main.cpp: Copy puts #copyInput's text on
 * the system clipboard; Paste shows the clipboard text in #pasteResult ("(empty)" when none).
 * The original reads getAttribute("value"), which never sees what the user typed; this port
 * copies the live value (kui_element_get_value), which is what the original means to do. */
#include "kui_c_example_dom.h"

#include <stdlib.h>

static KuiDocument* g_doc;

static void on_copy(KuiEvent* event, void* user_data) {
    char value[4096];
    (void)event;
    (void)user_data;
    kuex_get_value(g_doc, "copyInput", value, sizeof value);
    kui_platform_set_clipboard_text(value, 0);
}

static void on_paste(KuiEvent* event, void* user_data) {
    char* text = NULL;
    size_t length = 0;
    (void)event;
    (void)user_data;
    kui_platform_get_clipboard_text(NULL, 0, &length);
    if (length && (text = (char*)malloc(length + 1)) != NULL &&
        kui_platform_get_clipboard_text(text, length + 1, &length) != KUI_OK) {
        length = 0;
    }
    kuex_set_text(g_doc, "pasteResult", length && text ? text : "(empty)");
    free(text);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    return kuex_on(g_doc, "copyBtn", "click", on_copy, NULL) |
           kuex_on(g_doc, "pasteBtn", "click", on_paste, NULL);
}
