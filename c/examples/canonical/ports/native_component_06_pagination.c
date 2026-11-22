/* Host logic of examples/native/component/06_pagination/main.cpp: page buttons 1..5 and the
 * prev / next arrows move the current page and refresh the buttons, content and info line. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;
static int g_page = 1;

static void update(void) {
    char id[16];
    char html[96];
    char info[32];
    int i;
    KuiElement* content;
    for (i = 1; i <= 5; ++i) {
        snprintf(id, sizeof id, "pg%d", i);
        kuex_set_class(g_doc, id, i == g_page ? "pg active" : "pg");
    }
    snprintf(html, sizeof html, "<p>Page %d: Items %d-%d of 50.</p>", g_page,
             (g_page - 1) * 10 + 1, g_page * 10);
    content = kuex_by_id(g_doc, "content");
    if (content) {
        kui_element_set_inner_html(content, html, 0);
        kui_element_release(content);
    }
    snprintf(info, sizeof info, "Page %d of 5", g_page);
    kuex_set_text(g_doc, "info", info);
    kuex_set_class(g_doc, "prev", g_page <= 1 ? "pg disabled" : "pg");
    kuex_set_class(g_doc, "next", g_page >= 5 ? "pg disabled" : "pg");
}

static void on_page(KuiEvent* event, void* user_data) {
    (void)event;
    g_page = (int)(intptr_t)user_data;
    update();
}

static void on_prev(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    if (g_page > 1) { --g_page; update(); }
}

static void on_next(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    if (g_page < 5) { ++g_page; update(); }
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    char id[16];
    int i;
    g_doc = host->document;
    for (i = 1; i <= 5; ++i) {
        snprintf(id, sizeof id, "pg%d", i);
        if (kuex_on(g_doc, id, "click", on_page, (void*)(intptr_t)i) != 0) return 1;
    }
    return kuex_on(g_doc, "prev", "click", on_prev, NULL) |
           kuex_on(g_doc, "next", "click", on_next, NULL);
}
