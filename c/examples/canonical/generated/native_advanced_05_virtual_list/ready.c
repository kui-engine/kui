/* Copied by scripts/generate_c_examples.py from examples/c/ports/native_advanced_05_virtual_list.c; edit that file, not this copy. */
/* Host logic of examples/native/advanced/05_virtual_list/main.cpp: a 10,000-row VirtualList in
 * #listContainer (42px rows, overscan 5), Top / Item 5,000 / Bottom buttons, and a 250 ms
 * interval that reports the live DOM range in #stats. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;
static KuiVirtualList* g_list;

static KuiElement* render_row(KuiDocument* document, uint32_t index, void* user_data) {
    KuiElement* row = NULL;
    char html[128];
    (void)user_data;
    if (kui_document_create_element(document, "div", 0, &row) != KUI_OK) return NULL;
    kui_element_set_class_name(row, "row", 0);
    kui_element_set_style_property(row, "height", 0, "42px", 0, 0);
    snprintf(html, sizeof html, "<span class=\"idx\">#%u</span>Virtualized item %u",
             index + 1, index + 1);
    kui_element_set_inner_html(row, html, 0);
    return row;
}

static void update_stats(void* user_data) {
    KuiCVirtualListState state;
    char text[160];
    (void)user_data;
    memset(&state, 0, sizeof state);
    if (kui_virtual_list_get_state(g_list, &state) != KUI_OK) return;
    snprintf(text, sizeof text,
             "10,000 logical items \xC2\xB7 live DOM range [%u, %u) \xC2\xB7 %u rows rendered",
             state.visible_start, state.visible_end, state.visible_end - state.visible_start);
    kuex_set_text(g_doc, "stats", text);
}

static void on_top(KuiEvent* event, void* user_data) {
    (void)event;
    kui_virtual_list_scroll_to_top(g_list);
    update_stats(user_data);
}

static void on_middle(KuiEvent* event, void* user_data) {
    (void)event;
    kui_virtual_list_scroll_to_index(g_list, 4999);
    update_stats(user_data);
}

static void on_bottom(KuiEvent* event, void* user_data) {
    (void)event;
    kui_virtual_list_scroll_to_bottom(g_list);
    update_stats(user_data);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    KuiCVirtualListConfig config;
    KuiElement* container;
    KuiStatus status;
    g_doc = host->document;
    container = kuex_by_id(g_doc, "listContainer");
    if (!container) return 1;
    memset(&config, 0, sizeof config);
    config.total_items = 10000;
    config.item_height = 42.0f;
    config.overscan = 5;
    config.render_item = render_row;
    status = kui_virtual_list_create(container, &config, &g_list);
    kui_element_release(container);
    if (status != KUI_OK) return 1;
    if (kuex_on(g_doc, "topBtn", "click", on_top, NULL) |
        kuex_on(g_doc, "middleBtn", "click", on_middle, NULL) |
        kuex_on(g_doc, "bottomBtn", "click", on_bottom, NULL)) return 1;
    return kui_platform_set_interval(update_stats, NULL, 250, NULL) == KUI_OK ? 0 : 1;
}
