/* Host logic of examples/native/component/12_tooltip_and_tabindex/main.cpp: logs focus / blur of
 * every .fcard and Tab / Shift+Tab keydowns at the root into #log (newest line first). The title
 * tooltips and the Tab focus walk are engine-native and need no host code. */
#include "kui_c_example_dom.h"

#include <stdlib.h>

static KuiDocument* g_doc;

static void log_line(const char* line) {
    KuiElement* log = kuex_by_id(g_doc, "log");
    size_t need = 0;
    char* old_text;
    char* text;
    if (!log) return;
    kui_element_get_text_content(log, NULL, 0, &need);
    old_text = (char*)malloc(need + 1);
    text = (char*)malloc(strlen(line) + need + 2);
    if (old_text && text) {
        kuex_element_text(log, old_text, need + 1);
        snprintf(text, strlen(line) + need + 2, "%s\n%s", line, old_text);
        kui_element_set_text_content(log, text, 0);
    }
    free(old_text);
    free(text);
    kui_element_release(log);
}

/* user_data is a heap copy of the card's data-name; kept for the life of the example. */
static void on_focus(KuiEvent* event, void* user_data) {
    char line[160];
    (void)event;
    snprintf(line, sizeof line, "[focus] %s", (const char*)user_data);
    log_line(line);
}

static void on_blur(KuiEvent* event, void* user_data) {
    char line[160];
    (void)event;
    snprintf(line, sizeof line, "[blur]  %s", (const char*)user_data);
    log_line(line);
}

static void on_keydown(KuiEvent* event, void* user_data) {
    KuiCEventTypeTag tag = KUI_C_EVENT_BASE;
    char key[32];
    size_t length = 0;
    int shift = 0;
    (void)user_data;
    if (kui_event_get_type_tag(event, &tag) != KUI_OK || tag != KUI_C_EVENT_KEYBOARD) return;
    if (kui_event_key_get_key(event, key, sizeof key, &length) != KUI_OK) return;
    if (strcmp(key, "Tab") != 0) return;
    kui_event_key_get_modifiers(event, NULL, &shift, NULL, NULL);
    log_line(shift ? "[keydown] Shift+Tab -> focus moves back" : "[keydown] Tab -> focus advances");
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    KuiElement* cards[16];
    KuiElement* root = NULL;
    size_t count = 0;
    size_t i;
    KuiStatus status;
    g_doc = host->document;
    if (kui_document_query_selector_all(g_doc, ".fcard", 0, cards, 16, &count) != KUI_OK) return 1;
    for (i = 0; i < count; ++i) {
        char name[128];
        size_t length = 0;
        char* copy;
        name[0] = '\0';
        kui_element_get_attribute(cards[i], "data-name", 0, name, sizeof name, &length);
        copy = (char*)malloc(strlen(name) + 1);
        if (copy) {
            memcpy(copy, name, strlen(name) + 1);
            kui_element_add_event_listener(cards[i], "focus", 0, on_focus, copy, NULL);
            kui_element_add_event_listener(cards[i], "blur", 0, on_blur, copy, NULL);
        }
        kui_element_release(cards[i]);
    }
    if (kui_document_root(g_doc, &root) != KUI_OK || !root) return 1;
    status = kui_element_add_event_listener(root, "keydown", 0, on_keydown, NULL, NULL);
    kui_element_release(root);
    return status == KUI_OK ? 0 : 1;
}
