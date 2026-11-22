/* Host logic of examples/native/platform/08_file_drag_drop/main.cpp: dragenter / dragover on the
 * root element highlight #zone (class "over"), dragleave removes it, and a drop of OS files
 * lists their full UTF-8 paths in #list. */
#include "kui_c_example_dom.h"

#include <stdlib.h>

static KuiDocument* g_doc;

static void zone_over(int on) {
    KuiElement* zone = kuex_by_id(g_doc, "zone");
    if (!zone) return;
    kui_element_class_list_remove(zone, "over", 0);
    if (on) kui_element_class_list_add(zone, "over", 0);
    kui_element_release(zone);
}

static void on_drag_over(KuiEvent* event, void* user_data) {
    (void)user_data;
    kui_event_prevent_default(event); /* allows the drop */
    zone_over(1);
}

static void on_drag_leave(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    zone_over(0);
}

/* Appends text to a growing heap string; returns 0 when out of memory. */
static int append(char** html, size_t* size, const char* text) {
    size_t length = strlen(text);
    char* grown = (char*)realloc(*html, *size + length + 1);
    if (!grown) return 0;
    memcpy(grown + *size, text, length + 1);
    *html = grown;
    *size += length;
    return 1;
}

static void on_drop(KuiEvent* event, void* user_data) {
    KuiCEventTypeTag tag = KUI_C_EVENT_BASE;
    size_t count = 0;
    size_t i;
    char* html = NULL;
    size_t size = 0;
    char head[96];
    int ok;
    (void)user_data;
    zone_over(0);
    if (kui_event_get_type_tag(event, &tag) != KUI_OK || tag != KUI_C_EVENT_DRAG) return;
    if (kui_event_drag_get_file_count(event, &count) != KUI_OK || count == 0) return;
    kui_event_prevent_default(event);

    snprintf(head, sizeof head, "<div class=\"row count\">收到 %u 个项目:</div>", (unsigned)count);
    ok = append(&html, &size, head);
    for (i = 0; ok && i < count; ++i) {
        size_t length = 0;
        char* path;
        kui_event_drag_get_file(event, i, NULL, 0, &length);
        path = (char*)malloc(length + 1);
        if (!path) {
            ok = 0;
            break;
        }
        path[0] = '\0';
        kui_event_drag_get_file(event, i, path, length + 1, &length);
        ok = append(&html, &size, "<div class=\"row\">") && append(&html, &size, path) &&
             append(&html, &size, "</div>");
        free(path);
    }
    if (ok) {
        KuiElement* list = kuex_by_id(g_doc, "list");
        if (list) {
            kui_element_set_inner_html(list, html, 0);
            kui_element_release(list);
        }
    }
    free(html);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    KuiElement* root = NULL;
    int failed = 0;
    g_doc = host->document;
    if (kui_document_root(g_doc, &root) != KUI_OK || !root) return 1;
    failed |= kui_element_add_event_listener(root, "dragover", 0, on_drag_over, NULL, NULL) != KUI_OK;
    failed |= kui_element_add_event_listener(root, "dragenter", 0, on_drag_over, NULL, NULL) != KUI_OK;
    failed |= kui_element_add_event_listener(root, "dragleave", 0, on_drag_leave, NULL, NULL) != KUI_OK;
    failed |= kui_element_add_event_listener(root, "drop", 0, on_drop, NULL, NULL) != KUI_OK;
    kui_element_release(root);
    return failed;
}
