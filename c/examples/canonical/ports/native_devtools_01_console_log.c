/* Host logic of examples/native/devtools/01_console_log/main.cpp: Log Info / Warn / Error append
 * a "log li|lw|le" row to #output (Info reads "User action #<rows added so far>"), Clear empties
 * it; F12 DevTools is enabled and the window title gets the live DevTools URL appended. */
#include "kui_c_example_dom.h"

#include <stdlib.h>

static KuiDocument* g_doc;
static int g_log_count;

static void add_log(const char* cls, const char* msg) {
    KuiElement* output = kuex_by_id(g_doc, "output");
    KuiElement* entry = NULL;
    char class_name[32];
    if (output && kui_document_create_element(g_doc, "div", 0, &entry) == KUI_OK) {
        snprintf(class_name, sizeof class_name, "log %s", cls);
        kui_element_set_class_name(entry, class_name, 0);
        kui_element_set_text_content(entry, msg, 0);
        kui_element_append_child(output, entry);
        kui_element_release(entry);
    }
    if (output) kui_element_release(output);
    ++g_log_count;
}

static void on_info(KuiEvent* e, void* u) {
    char msg[48];
    (void)e; (void)u;
    snprintf(msg, sizeof msg, "User action #%d", g_log_count);
    add_log("li", msg);
}
static void on_warn(KuiEvent* e, void* u) {
    (void)e; (void)u;
    add_log("lw", "Warning: potential issue");
}
static void on_error(KuiEvent* e, void* u) {
    (void)e; (void)u;
    add_log("le", "Error: operation failed");
}
static void on_clear(KuiEvent* e, void* u) {
    KuiElement* output = kuex_by_id(g_doc, "output");
    (void)e; (void)u;
    if (!output) return;
    kui_element_set_inner_html(output, "", 0);
    kui_element_release(output);
}

/* examples/native/devtools/devtools_runtime.h startLiveDevTools: the runtime already started
 * DevTools for this example; append its URL to the title (or say that it failed). */
static void title_with_devtools_url(KuiWindow* window) {
    size_t title_length = 0;
    size_t url_length = 0;
    int have_url;
    char* title;
    kui_window_get_title(window, NULL, 0, &title_length);
    have_url = kui_window_get_devtools_url(window, NULL, 0, &url_length) != KUI_ERR_NOT_FOUND &&
               url_length > 0;
    title = (char*)malloc(title_length + url_length + 64);
    if (!title) return;
    kui_window_get_title(window, title, title_length + 1, &title_length);
    if (have_url) {
        memcpy(title + title_length, " — ", strlen(" — "));
        kui_window_get_devtools_url(window, title + title_length + strlen(" — "), url_length + 1,
                                    &url_length);
    } else {
        snprintf(title + title_length, 64, " — DevTools failed to start");
    }
    kui_window_set_title(window, title, 0);
    free(title);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    kui_platform_set_f12_devtools_enabled(1);
    title_with_devtools_url(host->window);
    return kuex_on(g_doc, "btnI", "click", on_info, NULL) |
           kuex_on(g_doc, "btnW", "click", on_warn, NULL) |
           kuex_on(g_doc, "btnE", "click", on_error, NULL) |
           kuex_on(g_doc, "btnC", "click", on_clear, NULL);
}
