/* Host logic of examples/dashboard/main.cpp: fills #table-body with seven .table-row rows, makes
 * the clicked nav item the only .active one, and Refresh writes a random 1400..1599 session
 * count into #stat-sessions. */
#include "kui_c_example_dom.h"

#include <stdlib.h>

typedef struct RowData {
    const char* name;
    const char* status;
    const char* badge_class;
    const char* value;
    const char* change;
    int positive;
} RowData;

static const RowData k_rows[] = {
    {"Project Alpha", "Active", "badge-success", "$12,400", "+5.2%", 1},
    {"Widget Pro", "Active", "badge-success", "$8,200", "+12.1%", 1},
    {"API Gateway", "Warning", "badge-warning", "$4,800", "-1.5%", 0},
    {"Mobile App", "Active", "badge-success", "$15,300", "+8.7%", 1},
    {"Auth Service", "Critical", "badge-danger", "$2,100", "-15.3%", 0},
    {"Data Pipeline", "Active", "badge-success", "$6,700", "+3.4%", 1},
    {"ML Platform", "Warning", "badge-warning", "$9,500", "+0.2%", 1},
};

static const char* const k_nav_ids[] = {"nav-overview", "nav-analytics", "nav-reports",
                                        "nav-settings"};

static KuiDocument* g_doc;

static void on_nav(KuiEvent* event, void* user_data) {
    KuiElement* target = NULL;
    size_t i;
    (void)user_data;
    for (i = 0; i < sizeof k_nav_ids / sizeof k_nav_ids[0]; ++i) {
        KuiElement* item = kuex_by_id(g_doc, k_nav_ids[i]);
        if (!item) continue;
        kui_element_class_list_remove(item, "active", 0);
        kui_element_release(item);
    }
    if (kui_event_get_target(event, &target) == KUI_OK && target) {
        kui_element_class_list_add(target, "active", 0);
        kui_element_release(target);
    }
}

static void on_refresh(KuiEvent* event, void* user_data) {
    char text[16];
    (void)event;
    (void)user_data;
    snprintf(text, sizeof text, "%d", 1400 + rand() % 200);
    kuex_set_text(g_doc, "stat-sessions", text);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    KuiElement* body;
    size_t i;
    int failed = 0;
    g_doc = host->document;

    body = kuex_by_id(g_doc, "table-body");
    if (!body) return 1;
    for (i = 0; i < sizeof k_rows / sizeof k_rows[0]; ++i) {
        const RowData* row = &k_rows[i];
        KuiElement* row_elem = NULL;
        char html[512];
        if (kui_document_create_element(g_doc, "div", 0, &row_elem) != KUI_OK) {
            failed = 1;
            break;
        }
        kui_element_set_class_name(row_elem, "table-row", 0);
        snprintf(html, sizeof html,
                 "<div class=\"col-name\">%s</div>"
                 "<div class=\"col-status\"><span class=\"badge %s\">%s</span></div>"
                 "<div class=\"col-value\">%s</div>"
                 "<div class=\"col-change\" style=\"color: %s\">%s</div>",
                 row->name, row->badge_class, row->status, row->value,
                 row->positive ? "var(--success)" : "var(--danger)", row->change);
        kui_element_set_inner_html(row_elem, html, 0);
        kui_element_append_child(body, row_elem);
        kui_element_release(row_elem);
    }
    kui_element_release(body);
    if (failed) return 1;

    for (i = 0; i < sizeof k_nav_ids / sizeof k_nav_ids[0]; ++i)
        failed |= kuex_on(g_doc, k_nav_ids[i], "click", on_nav, NULL);
    return failed | kuex_on(g_doc, "refreshBtn", "click", on_refresh, NULL);
}
