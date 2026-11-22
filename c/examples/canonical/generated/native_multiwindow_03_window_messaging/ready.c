/* Copied by scripts/generate_c_examples.py from examples/c/ports/native_multiwindow_03_window_messaging.c; edit that file, not this copy. */
/* Host logic of examples/native/multiwindow/03_window_messaging/main.cpp: Window A's Send button
 * counts messages, logs "Sent message #N" in #log and "Received message #N from Window A" in
 * Window B's #log2.
 *
 * The generated page only carries Window A; the runner's second window shows a placeholder, so
 * this port loads the original Window B page (and title) into it first. */
#include "kui_c_example_dom.h"

static const char k_window_b_html[] =
    "<!DOCTYPE html><html><head><style>\n"
    "*{margin:0;padding:0;box-sizing:border-box}\n"
    "body{padding:24px;font-family:\"Segoe UI\",sans-serif;background:#064e3b;color:#f8fafc}\n"
    "h2{margin-bottom:16px}\n"
    ".card{background:rgba(255,255,255,.1);border-radius:12px;padding:20px;border:1px solid rgba(255,255,255,.1)}\n"
    ".card h3{font-size:14px;margin-bottom:8px}\n"
    ".log{background:rgba(0,0,0,.3);border-radius:8px;padding:12px;font-size:12px;color:#a7f3d0;"
    "font-family:\"Consolas\",monospace;min-height:60px}\n"
    "</style></head><body>\n"
    "<h2>Window B \xE2\x80\x94 Receiver</h2>\n"
    "<div class=\"card\"><h3>Messages</h3><div class=\"log\" id=\"log2\">Waiting for messages...</div></div>\n"
    "</body></html>";

static KuiDocument* g_doc;
static KuiDocument* g_doc2;
static int g_count;

static void on_send(KuiEvent* event, void* user_data) {
    char text[96];
    (void)event;
    (void)user_data;
    ++g_count;
    snprintf(text, sizeof text, "Sent message #%d", g_count);
    kuex_set_text(g_doc, "log", text);
    snprintf(text, sizeof text, "Received message #%d from Window A", g_count);
    kuex_set_text(g_doc2, "log2", text);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    g_doc2 = host->second_document;
    if (!g_doc2 || kui_document_load_html(g_doc2, k_window_b_html, 0) != KUI_OK) return 1;
    kui_window_set_title(host->second_window, "KUI \xE2\x80\x94 Window B", 0);
    return kuex_on(g_doc, "sendBtn", "click", on_send, NULL);
}
