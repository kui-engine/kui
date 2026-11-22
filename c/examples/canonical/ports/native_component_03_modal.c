/* Host logic of examples/native/component/03_modal/main.cpp: the two open buttons fill and show
 * the modal; Cancel / Confirm hide it and report the choice in the status line. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;

static void on_open_confirm(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    kuex_set_text(g_doc, "mT", "Confirm Action");
    kuex_set_text(g_doc, "mM", "Are you sure you want to proceed?");
    kuex_set_class(g_doc, "okBtn", "btn bp");
    kuex_set_class(g_doc, "ov", "overlay show");
}

static void on_open_delete(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    kuex_set_text(g_doc, "mT", "Delete Item");
    kuex_set_text(g_doc, "mM", "This cannot be undone. Delete permanently?");
    kuex_set_class(g_doc, "okBtn", "btn bd");
    kuex_set_class(g_doc, "ov", "overlay show");
}

/* user_data is the status text to show after closing. */
static void on_close(KuiEvent* event, void* user_data) {
    (void)event;
    kuex_set_class(g_doc, "ov", "overlay");
    kuex_set_text(g_doc, "status", (const char*)user_data);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    return kuex_on(g_doc, "openC", "click", on_open_confirm, NULL) |
           kuex_on(g_doc, "openD", "click", on_open_delete, NULL) |
           kuex_on(g_doc, "canBtn", "click", on_close, (void*)"Cancelled.") |
           kuex_on(g_doc, "okBtn", "click", on_close, (void*)"Confirmed!");
}
