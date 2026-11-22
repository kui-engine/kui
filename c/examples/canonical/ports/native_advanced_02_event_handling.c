/* Host logic of examples/native/advanced/02_event_handling/main.cpp: click / mouseenter /
 * mouseleave on the target each append a log entry (click with its client coordinates) and
 * scroll the log to the bottom. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;

static void add_log(const char* type, const char* detail) {
    KuiElement* log = kuex_by_id(g_doc, "log");
    KuiElement* entry = NULL;
    char html[256];
    float left = 0, top = 0, width = 0, height = 0;
    if (!log) return;
    if (kui_document_create_element(g_doc, "div", 0, &entry) == KUI_OK) {
        kui_element_set_class_name(entry, "log-entry", 0);
        snprintf(html, sizeof html, "<span class=\"type\">%s</span> %s", type, detail);
        kui_element_set_inner_html(entry, html, 0);
        kui_element_append_child(log, entry);
        kui_element_release(entry);
        kui_element_get_scroll_metrics(log, &left, &top, &width, &height);
        kui_element_set_scroll_position(log, left, height);
    }
    kui_element_release(log);
}

static void on_click(KuiEvent* event, void* user_data) {
    float x = 0, y = 0;
    char detail[64];
    (void)user_data;
    kui_event_mouse_get_client_xy(event, &x, &y);
    snprintf(detail, sizeof detail, "x=%d y=%d", (int)x, (int)y);
    add_log("click", detail);
}

static void on_enter(KuiEvent* event, void* user_data) { (void)event; (void)user_data; add_log("mouseenter", ""); }
static void on_leave(KuiEvent* event, void* user_data) { (void)event; (void)user_data; add_log("mouseleave", ""); }

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    return kuex_on(g_doc, "target", "click", on_click, NULL) |
           kuex_on(g_doc, "target", "mouseenter", on_enter, NULL) |
           kuex_on(g_doc, "target", "mouseleave", on_leave, NULL);
}
