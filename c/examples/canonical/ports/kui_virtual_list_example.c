/* Host logic of examples/virtual_list_example/main.cpp: a 100,000-row VirtualList in
 * #vlist-container (32px rows, overscan 5) with insert / remove / scroll buttons, a 50,000-item
 * single-selection ListView in #listview-container, and the performance stats in #stats-content. */
#include "kui_c_example_dom.h"

#include <stdlib.h>
#include <time.h>

static KuiDocument* g_doc;
static KuiVirtualList* g_vlist;
static KuiListView* g_list_view;
static uint32_t g_insert_count;
static double g_setup_ms;
static double g_insert_ms;
static int g_insert_ops;

static double now_ms(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

/* ---- Stats display ---------------------------------------------------------------------- */

static void add_line(KuiElement* box, const char* text, const char* color) {
    const char* styles[] = {"color", color, "font-family", "'Consolas', monospace",
                            "font-size", "12px", "margin-bottom", "2px", NULL};
    kuex_append_release(box, kuex_make_element(g_doc, "div", text, styles));
}

static void update_stats_display(void) {
    KuiCVirtualListState state;
    KuiElement* box = kuex_by_id(g_doc, "stats-content");
    char buf[256];
    if (!box) return;
    kui_element_set_text_content(box, "", 0); /* clear existing content */
    memset(&state, 0, sizeof state);
    kui_virtual_list_get_state(g_vlist, &state);

    snprintf(buf, sizeof buf, "Total items: %u", state.total_items);
    add_line(box, buf, "#38bdf8");
    snprintf(buf, sizeof buf, "Visible range: [%u, %u) (%u rendered)", state.visible_start,
             state.visible_end, state.visible_end - state.visible_start);
    add_line(box, buf, "#94a3b8");
    snprintf(buf, sizeof buf, "Recycle pool: %u elements", state.recycle_pool_size);
    add_line(box, buf, "#94a3b8");
    snprintf(buf, sizeof buf, "Total height: %.0f px", state.total_height);
    add_line(box, buf, "#94a3b8");
    snprintf(buf, sizeof buf, "Setup time: %.2f ms", g_setup_ms);
    add_line(box, buf, g_setup_ms < 16 ? "#4ade80" : "#f87171");
    if (g_insert_ops > 0) {
        snprintf(buf, sizeof buf, "Last insert: %.2f ms (%d ops, %u items added)", g_insert_ms,
                 g_insert_ops, g_insert_count);
        add_line(box, buf, g_insert_ms < 50 ? "#4ade80" : "#f87171");
    }
    kui_element_release(box);
}

/* ---- Button handlers -------------------------------------------------------------------- */

static uint32_t total_items(void) {
    KuiCVirtualListState state;
    memset(&state, 0, sizeof state);
    kui_virtual_list_get_state(g_vlist, &state);
    return state.total_items;
}

static void timed_insert(uint32_t index, const char* where) {
    double t0 = now_ms();
    kui_virtual_list_insert_items(g_vlist, index, 1000);
    g_insert_count += 1000;
    g_insert_ms = now_ms() - t0;
    g_insert_ops++;
    update_stats_display();
    fprintf(stderr, "[VList] Inserted 1000 items at %s, took %.2f ms\n", where, g_insert_ms);
}

static void on_insert_middle(KuiEvent* event, void* user_data) {
    char where[16];
    uint32_t mid = total_items() / 2;
    (void)event;
    (void)user_data;
    snprintf(where, sizeof where, "%u", mid);
    timed_insert(mid, where);
}

static void on_insert_beginning(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    timed_insert(0, "0");
}

static void on_insert_end(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    timed_insert(total_items(), "end");
}

static void on_remove_middle(KuiEvent* event, void* user_data) {
    double t0;
    uint32_t mid;
    (void)event;
    (void)user_data;
    if (total_items() < 1000) return;
    t0 = now_ms();
    mid = total_items() / 2;
    kui_virtual_list_remove_items(g_vlist, mid, 1000);
    g_insert_ms = now_ms() - t0;
    g_insert_ops++;
    update_stats_display();
    fprintf(stderr, "[VList] Removed 1000 items at %u, took %.2f ms\n", mid, g_insert_ms);
}

static void on_scroll_to_random(KuiEvent* event, void* user_data) {
    uint32_t total = total_items();
    uint32_t idx;
    (void)event;
    (void)user_data;
    if (total == 0) return;
    idx = (uint32_t)(((unsigned long long)rand() * (RAND_MAX + 1ull) + (unsigned)rand()) % total);
    kui_virtual_list_scroll_to_index(g_vlist, idx);
    update_stats_display();
    fprintf(stderr, "[VList] Scrolled to index %u\n", idx);
}

static void on_refresh_stats(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    update_stats_display();
}

/* ---- Virtual components ----------------------------------------------------------------- */

static KuiElement* render_row(KuiDocument* document, uint32_t index, void* user_data) {
    const char* row_styles[] = {
        "display", "flex", "align-items", "center", "padding", "0 12px", "font-size", "13px",
        "color", index % 2 ? "#94a3b8" : "#cbd5e1",
        "background", index % 2 ? "transparent" : "rgba(255,255,255,0.02)",
        "font-family", "'Consolas', monospace", NULL};
    const char* idx_styles[] = {"color", "#475569", "width", "70px", "font-size", "11px",
                                "flex-shrink", "0", NULL};
    const char* text_styles[] = {"flex", "1", NULL};
    const char* badge_styles[] = {"font-size", "10px", "color", "#475569", "flex-shrink", "0",
                                  NULL};
    KuiElement* row = kuex_make_element(document, "div", NULL, row_styles);
    char text[96];
    (void)user_data;
    if (!row) return NULL;
    snprintf(text, sizeof text, "%u", index);
    kuex_append_release(row, kuex_make_element(document, "span", text, idx_styles));
    snprintf(text, sizeof text, "Item #%u%s%s", index,
             index % 1000 == 0 ? "  ** milestone **" : "",
             index % 10000 == 0 ? "  >>> MAJOR <<<" : "");
    kuex_append_release(row, kuex_make_element(document, "span", text, text_styles));
    kuex_append_release(row, kuex_make_element(document, "span", index % 2 == 0 ? "even" : "odd",
                                               badge_styles));
    return row;
}

static void list_item(uint32_t index, KuiCListViewItem* item, void* user_data) {
    static const char* const icons[] = {"\xf0\x9f\x93\x84", "\xf0\x9f\x93\x81",
                                        "\xf0\x9f\x94\xa7", "\xf0\x9f\x93\xa6",
                                        "\xf0\x9f\x8c\x90", "\xe2\x9a\xa1"};
    static char text[48];
    static char subtitle[64];
    (void)user_data;
    snprintf(text, sizeof text, "List item %u", index);
    snprintf(subtitle, sizeof subtitle, "Description for item %u", index);
    item->icon = icons[index % 6];
    item->text = text;
    item->subtitle = subtitle;
    item->disabled = index % 50 == 49;
}

static void on_select(uint32_t index, void* user_data) {
    (void)user_data;
    fprintf(stderr, "[ListView] Selected: %u\n", index);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    KuiCVirtualListConfig vconfig;
    KuiCListViewConfig lconfig;
    KuiElement* container;
    double t0;
    g_doc = host->document;
    srand((unsigned)time(NULL));

    /* Wire up buttons */
    if (kuex_on(g_doc, "btn-insert-mid", "click", on_insert_middle, NULL) |
        kuex_on(g_doc, "btn-insert-begin", "click", on_insert_beginning, NULL) |
        kuex_on(g_doc, "btn-insert-end", "click", on_insert_end, NULL) |
        kuex_on(g_doc, "btn-remove-mid", "click", on_remove_middle, NULL) |
        kuex_on(g_doc, "btn-scroll-random", "click", on_scroll_to_random, NULL) |
        kuex_on(g_doc, "btn-refresh-stats", "click", on_refresh_stats, NULL)) return 1;

    /* VirtualList: 100,000 rows */
    container = kuex_by_id(g_doc, "vlist-container");
    if (!container) return 1;
    memset(&vconfig, 0, sizeof vconfig);
    vconfig.total_items = 100000;
    vconfig.item_height = 32.0f;
    vconfig.overscan = 5;
    vconfig.render_item = render_row;
    t0 = now_ms();
    if (kui_virtual_list_create(container, &vconfig, &g_vlist) != KUI_OK) g_vlist = NULL;
    g_setup_ms = now_ms() - t0;
    kui_element_release(container);
    if (!g_vlist) return 1;
    fprintf(stderr, "[VList] Setup 100K items in %.2f ms\n", g_setup_ms);

    /* ListView: 50,000 items with selection */
    container = kuex_by_id(g_doc, "listview-container");
    if (!container) return 1;
    memset(&lconfig, 0, sizeof lconfig);
    lconfig.item_count = 50000;
    lconfig.item_height = 36.0f;
    lconfig.overscan = 5;
    lconfig.selection_mode = KUI_C_SELECTION_SINGLE;
    lconfig.item_provider = list_item;
    lconfig.on_select = on_select;
    if (kui_list_view_create(container, &lconfig, &g_list_view) != KUI_OK) g_list_view = NULL;
    kui_element_release(container);
    if (!g_list_view) return 1;

    update_stats_display(); /* initial stats display */
    return 0;
}
