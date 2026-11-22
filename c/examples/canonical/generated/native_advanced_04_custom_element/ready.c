/* Copied by scripts/generate_c_examples.py from examples/c/ports/native_advanced_04_custom_element.c; edit that file, not this copy. */
/* Host logic of examples/native/advanced/04_custom_element/main.cpp: defines kui-card and
 * kui-badge with lifecycle counters before the page loads (before_load), so parsing the page
 * runs the callbacks as in the original; once loaded, marks the first badge with
 * data-live="registered" and writes the counters into #lifecycle. */
#include "kui_c_example_dom.h"

static int g_connected;
static int g_disconnected;
static int g_attributes;

static void on_connected(KuiElement* element, void* user_data) {
    (void)element;
    (void)user_data;
    ++g_connected;
}

static void on_disconnected(KuiElement* element, void* user_data) {
    (void)element;
    (void)user_data;
    ++g_disconnected;
}

static void on_attribute(KuiElement* element, const char* name, size_t name_len,
                         const char* old_value, size_t old_len, const char* new_value,
                         size_t new_len, void* user_data) {
    (void)element; (void)name; (void)name_len; (void)old_value; (void)old_len;
    (void)new_value; (void)new_len; (void)user_data;
    ++g_attributes;
}

int kui_c_example_before_load(const KuiCExampleHost* host) {
    static const char* const tags[] = {"kui-card", "kui-badge"};
    KuiCCustomElementCallbacks callbacks;
    size_t i;
    memset(&callbacks, 0, sizeof callbacks);
    callbacks.connected = on_connected;
    callbacks.disconnected = on_disconnected;
    callbacks.attribute_changed = on_attribute;
    for (i = 0; i < sizeof tags / sizeof tags[0]; ++i) {
        if (kui_document_define_custom_element(host->document, tags[i], 0, &callbacks) != KUI_OK)
            return 1;
    }
    return 0;
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    KuiDocument* doc = host->document;
    KuiElement* badge = NULL;
    char line[256];
    if (kui_document_query_selector(doc, "kui-badge", 0, &badge) == KUI_OK && badge) {
        kui_element_set_attribute(badge, "data-live", 0, "registered", 0);
        kui_element_release(badge);
    }
    snprintf(line, sizeof line,
             "Registry definitions: kui-card, kui-badge \xC2\xB7 connected=%d \xC2\xB7 "
             "disconnected=%d \xC2\xB7 attribute callbacks=%d",
             g_connected, g_disconnected, g_attributes);
    kuex_set_text(doc, "lifecycle", line);
    return 0;
}
