/* Copied by scripts/generate_c_examples.py from examples/c/ports/kui_api_widgets.c; edit that file, not this copy. */
/* API tour: native widgets (examples/api_tours/widgets). The host mounts two ListViews (single
 * and multi selection), a multi-select TreeView and a VirtualList into the page, calls every
 * selection / expand / scroll / data function once and checks the state read back (selected
 * set, focus, node and visible row counts, rendered range); #tour-result ends as "PASS n/n".
 * The panel buttons repeat the calls and print the state into each panel's readout. */
#include "kui_c_example_dom.h"

#include <stdarg.h>

static KuiDocument* g_doc;

/* ---- Self-check bookkeeping: count, log one <tr> per check, publish the verdict ----------
 * check():  a documented behaviour; a failure makes the tour FAIL.
 * known():  a documented behaviour the engine currently gets wrong (reported bug). A failure is
 *           logged as "KNOWN BUG" and kept out of the verdict; once it passes it counts as ok. */

static int g_total;
static int g_failed;
static int g_known;
static char g_failed_names[512];

static void log_row(const char* name, const char* result, const char* cell) {
    KuiElement* log = kuex_by_id(g_doc, "tour-log");
    KuiElement* row = NULL;
    if (!log) return;
    if (kui_document_create_element(g_doc, "tr", 0, &row) == KUI_OK) {
        kui_element_set_attribute(row, "data-check", 0, name, 0);
        kui_element_set_attribute(row, "data-result", 0, result, 0);
        if (strcmp(result, "ok") != 0) kui_element_class_list_add(row, "bad", 0);
        kuex_append_release(row, kuex_make_element(g_doc, "td", name, NULL));
        kuex_append_release(row, kuex_make_element(g_doc, "td", cell, NULL));
        kuex_append_release(log, row);
    }
    kui_element_release(log);
}

static void check(const char* name, int ok, const char* fmt, ...) {
    char detail[256];
    char cell[300];
    size_t used = strlen(g_failed_names);
    va_list args;

    g_total++;
    if (ok) {
        log_row(name, "ok", "ok");
        return;
    }
    va_start(args, fmt);
    vsnprintf(detail, sizeof detail, fmt, args);
    va_end(args);
    g_failed++;
    snprintf(g_failed_names + used, sizeof g_failed_names - used, "%s%s", used ? ", " : "", name);
    snprintf(cell, sizeof cell, "FAIL: %s", detail);
    fprintf(stderr, "tour check failed: %s: %s\n", name, detail);
    log_row(name, "fail", cell);
}

static void known(const char* name, int ok, const char* bug, const char* fmt, ...) {
    char detail[256];
    char cell[400];
    va_list args;

    if (ok) {
        g_total++;
        log_row(name, "ok", "ok");
        return;
    }
    va_start(args, fmt);
    vsnprintf(detail, sizeof detail, fmt, args);
    va_end(args);
    g_known++;
    snprintf(cell, sizeof cell, "KNOWN BUG: %s (%s)", bug, detail);
    fprintf(stderr, "tour known bug: %s: %s\n", name, detail);
    log_row(name, "known", cell);
}

static void finish(void) {
    char text[640];
    char number[16];
    size_t used;
    KuiElement* result = kuex_by_id(g_doc, "tour-result");
    if (!result) return;
    if (g_failed == 0) {
        snprintf(text, sizeof text, "PASS %d/%d", g_total, g_total);
    } else {
        snprintf(text, sizeof text, "FAIL %d/%d: %s", g_failed, g_total, g_failed_names);
    }
    used = strlen(text);
    if (g_known) snprintf(text + used, sizeof text - used, " (known bugs: %d)", g_known);
    kui_element_set_text_content(result, text, 0);
    snprintf(number, sizeof number, "%d", g_total);
    kui_element_set_attribute(result, "data-total", 0, number, 0);
    snprintf(number, sizeof number, "%d", g_failed);
    kui_element_set_attribute(result, "data-failed", 0, number, 0);
    snprintf(number, sizeof number, "%d", g_known);
    kui_element_set_attribute(result, "data-known", 0, number, 0);
    kui_element_class_list_add(result, g_failed == 0 ? "pass" : "fail", 0);
    kui_element_release(result);
}

/* ---- Small readers ---------------------------------------------------------------------- */

/* "[a,b,c]" from an ascending index list. */
static void format_indices(const uint32_t* values, size_t count, char* out, size_t capacity) {
    size_t used;
    size_t i;
    snprintf(out, capacity, "[");
    for (i = 0; i < count; ++i) {
        used = strlen(out);
        snprintf(out + used, capacity - used, "%s%u", i ? "," : "", values[i]);
    }
    used = strlen(out);
    snprintf(out + used, capacity - used, "]");
}

/* textContent of the element with that id contains needle. */
static int text_has(const char* id, const char* needle) {
    char text[4096];
    kuex_get_text(g_doc, id, text, sizeof text);
    return strstr(text, needle) != NULL;
}

static void set_textf(const char* id, const char* fmt, ...) {
    char text[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    kuex_set_text(g_doc, id, text);
}

/* ======================================================================================== */
/* ---- ListView: selection modes, range selection, item count, scrolling ----------------- */

#define LV_COUNT 200u
#define LV_ROW 30.0f
#define LV_OVERSCAN 2u

static KuiListView* g_single;
static KuiListView* g_multi;
static int g_lv_generation;    /* refresh: appended to every row label once > 0 */
static int g_lv_provided;      /* item_provider calls */
static uint32_t g_lv_count = LV_COUNT;

static void list_item(uint32_t index, KuiCListViewItem* item, void* user_data) {
    static char text[48];
    (void)user_data;
    g_lv_provided++;
    if (g_lv_generation > 0) {
        snprintf(text, sizeof text, "Row %u r%d", index, g_lv_generation);
    } else {
        snprintf(text, sizeof text, "Row %u", index);
    }
    item->text = text;
}

/* Selected indices of a list as "[...]"; returns the count. */
static size_t lv_selected(KuiListView* list, char* out, size_t capacity) {
    uint32_t values[256];
    size_t count = 0;
    kui_list_view_get_selected(list, values, 256, &count);
    format_indices(values, count < 256 ? count : 256, out, capacity);
    return count;
}

static int lv_is(KuiListView* list, uint32_t index) {
    int selected = -1;
    kui_list_view_is_selected(list, index, &selected);
    return selected;
}

static int32_t lv_focus(KuiListView* list) {
    int32_t index = -2;
    kui_list_view_get_focused_index(list, &index);
    return index;
}

static KuiCVirtualListState lv_state(KuiListView* list) {
    KuiCVirtualListState state;
    memset(&state, 0, sizeof state);
    kui_list_view_get_state(list, &state);
    return state;
}

static void show_list(void) {
    char text[256];
    KuiCVirtualListState state = lv_state(g_multi);
    lv_selected(g_single, text, sizeof text);
    kuex_set_text(g_doc, "lv-single-sel", text);
    lv_selected(g_multi, text, sizeof text);
    kuex_set_text(g_doc, "lv-multi-sel", text);
    set_textf("lv-focus", "%d", lv_focus(g_multi));
    set_textf("lv-start", "%u", state.visible_start);
    set_textf("lv-end", "%u", state.visible_end);
    set_textf("lv-total", "%u", state.total_items);
}

/* Clicks and keyboard selection on either list. */
static void on_list_select(uint32_t index, void* user_data) {
    set_textf("lv-event", "on_select(%u) %s", index, (const char*)user_data);
    show_list();
}

static void on_list_activate(uint32_t index, void* user_data) {
    set_textf("lv-event", "on_activate(%u) %s", index, (const char*)user_data);
}

static KuiListView* create_list(const char* id, KuiCSelectionMode mode, const char* name) {
    KuiCListViewConfig config;
    KuiListView* list = NULL;
    KuiElement* container = kuex_by_id(g_doc, id);
    if (!container) return NULL;
    memset(&config, 0, sizeof config);
    config.item_count = LV_COUNT;
    config.item_height = LV_ROW;
    config.overscan = LV_OVERSCAN;
    config.selection_mode = mode;
    config.item_bg = "#ffffff";
    config.item_hover_bg = "#f1f5f9";
    config.item_selected_bg = "#2563eb";
    config.item_text_color = "#0f172a";
    config.item_sub_color = "#64748b";
    config.item_provider = list_item;
    config.on_select = on_list_select;
    config.on_activate = on_list_activate;
    config.user_data = (void*)name;
    kui_list_view_create(container, &config, &list);
    kui_element_release(container);
    return list;
}

static void check_list_view(void) {
    char text[256];
    KuiCVirtualListState state = lv_state(g_multi);
    size_t count = 99;
    int before;

    check("list.create", g_single && g_multi && state.total_items == LV_COUNT &&
                             state.item_height == LV_ROW && state.total_height == 6000.0f &&
                             state.visible_start == 0 && state.visible_end > 0 &&
                             state.visible_end <= LV_COUNT && g_lv_provided > 0 &&
                             text_has("lv-multi", "Row 0"),
          "total %u row %.0f height %.0f rendered %u-%u provided %d", state.total_items,
          state.item_height, state.total_height, state.visible_start, state.visible_end,
          g_lv_provided);

    /* Single selection: a second select replaces the first. */
    kui_list_view_select(g_single, 2);
    kui_list_view_select(g_single, 4);
    lv_selected(g_single, text, sizeof text);
    check("list.select-single", strcmp(text, "[4]") == 0 && lv_is(g_single, 2) == 0 &&
                                    lv_is(g_single, 4) == 1 && lv_focus(g_single) == 4,
          "selected %s focus %d", text, lv_focus(g_single));

    /* Multi selection: selections accumulate. */
    kui_list_view_select(g_multi, 2);
    kui_list_view_select(g_multi, 4);
    lv_selected(g_multi, text, sizeof text);
    check("list.select-multi", strcmp(text, "[2,4]") == 0 && lv_is(g_multi, 2) == 1 &&
                                   lv_is(g_multi, 4) == 1 && lv_focus(g_multi) == 4,
          "selected %s focus %d", text, lv_focus(g_multi));

    /* kui_c.h: select_range adds rows [start, end). */
    kui_list_view_select_range(g_multi, 6, 9);
    lv_selected(g_multi, text, sizeof text);
    check("list.select_range", strcmp(text, "[2,4,6,7,8]") == 0 && lv_is(g_multi, 9) == 0,
          "selected %s, is_selected(9) %d", text, lv_is(g_multi, 9));

    kui_list_view_deselect(g_multi, 4);
    lv_selected(g_multi, text, sizeof text);
    check("list.deselect", strcmp(text, "[2,6,7,8]") == 0 && lv_is(g_multi, 4) == 0,
          "selected %s", text);

    /* Two-call form: buf NULL for the count. */
    kui_list_view_get_selected(g_multi, NULL, 0, &count);
    check("list.get_selected-count", count == 4, "count %u", (unsigned)count);

    kui_list_view_clear_selection(g_single);
    kui_list_view_clear_selection(g_multi);
    count = 99;
    kui_list_view_get_selected(g_multi, NULL, 0, &count);
    check("list.clear_selection", count == 0 && lv_is(g_multi, 2) == 0 && lv_is(g_single, 4) == 0,
          "count %u", (unsigned)count);

    kui_list_view_scroll_to_index(g_multi, 100);
    state = lv_state(g_multi);
    check("list.scroll_to_index", state.visible_start <= 100 && state.visible_end > 100 &&
                                      state.visible_start > 0,
          "rendered %u-%u", state.visible_start, state.visible_end);

    kui_list_view_scroll_to_bottom(g_multi);
    state = lv_state(g_multi);
    check("list.scroll_to_bottom", state.visible_end == LV_COUNT && state.visible_start > 100,
          "rendered %u-%u", state.visible_start, state.visible_end);

    kui_list_view_scroll_to_top(g_multi);
    state = lv_state(g_multi);
    check("list.scroll_to_top", state.visible_start == 0, "rendered %u-%u", state.visible_start,
          state.visible_end);

    /* Shrinking the list drops selected rows that no longer exist. */
    kui_list_view_select(g_multi, 10);
    kui_list_view_select(g_multi, 150);
    kui_list_view_set_item_count(g_multi, 100);
    state = lv_state(g_multi);
    lv_selected(g_multi, text, sizeof text);
    check("list.set_item_count", state.total_items == 100 && state.total_height == 3000.0f &&
                                     strcmp(text, "[10]") == 0,
          "total %u height %.0f selected %s", state.total_items, state.total_height, text);
    kui_list_view_set_item_count(g_multi, LV_COUNT);
    kui_list_view_clear_selection(g_multi);

    /* refresh re-asks the provider for the rows in view. */
    before = g_lv_provided;
    g_lv_generation = 1;
    kui_list_view_refresh(g_multi);
    kui_list_view_refresh(g_single);
    check("list.refresh", g_lv_provided > before && text_has("lv-multi", "Row 0 r1") &&
                              text_has("lv-single", "Row 0 r1"),
          "provider calls %d -> %d", before, g_lv_provided);
}

/* Panel buttons: the same calls on the visible lists. */
static void on_lv_select2(KuiEvent* e, void* u) {
    (void)e; (void)u;
    kui_list_view_select(g_single, 2);
    kui_list_view_select(g_multi, 2);
    show_list();
}

static void on_lv_select4(KuiEvent* e, void* u) {
    (void)e; (void)u;
    kui_list_view_select(g_single, 4);
    kui_list_view_select(g_multi, 4);
    show_list();
}

static void on_lv_range(KuiEvent* e, void* u) {
    (void)e; (void)u;
    kui_list_view_select_range(g_multi, 6, 9);
    show_list();
}

static void on_lv_deselect(KuiEvent* e, void* u) {
    (void)e; (void)u;
    kui_list_view_deselect(g_single, 2);
    kui_list_view_deselect(g_multi, 2);
    show_list();
}

static void on_lv_clear(KuiEvent* e, void* u) {
    (void)e; (void)u;
    kui_list_view_clear_selection(g_single);
    kui_list_view_clear_selection(g_multi);
    show_list();
}

static void on_lv_goto(KuiEvent* e, void* u) {
    (void)e; (void)u;
    kui_list_view_scroll_to_index(g_single, 100);
    kui_list_view_scroll_to_index(g_multi, 100);
    show_list();
}

static void on_lv_top(KuiEvent* e, void* u) {
    (void)e; (void)u;
    kui_list_view_scroll_to_top(g_single);
    kui_list_view_scroll_to_top(g_multi);
    show_list();
}

static void on_lv_bottom(KuiEvent* e, void* u) {
    (void)e; (void)u;
    kui_list_view_scroll_to_bottom(g_single);
    kui_list_view_scroll_to_bottom(g_multi);
    show_list();
}

static void on_lv_count(KuiEvent* e, void* u) {
    (void)e; (void)u;
    g_lv_count = g_lv_count == LV_COUNT ? 50u : LV_COUNT;
    kui_list_view_set_item_count(g_single, g_lv_count);
    kui_list_view_set_item_count(g_multi, g_lv_count);
    show_list();
}

static void on_lv_refresh(KuiEvent* e, void* u) {
    (void)e; (void)u;
    g_lv_generation++;
    kui_list_view_refresh(g_single);
    kui_list_view_refresh(g_multi);
    set_textf("lv-event", "refresh r%d", g_lv_generation);
}

/* ======================================================================================== */
/* ---- TreeView: nodes, expand / collapse, multi selection, scrolling --------------------- */

/* Node indices of the visible tree (added in this order):
 *   0 src { 1 core { 2 types.h, 3 string.h }, 4 ui { 5 button.c, 6 list.c }, 7 main.c }
 *   8 docs { 9 guide.md, 10 api.md }   11 README */
enum { N_SRC, N_CORE, N_TYPES, N_STRING, N_UI, N_BUTTON, N_LIST, N_MAIN, N_DOCS, N_GUIDE, N_API,
       N_README, N_NODES };

static KuiTreeView* g_tree;
static KuiTreeView* g_scratch_tree;
static char g_tree_selection[128];   /* last on_selection_changed */
static int g_tree_selection_calls;
static int64_t g_tree_last_select = -1;
static int64_t g_tree_last_toggle = -1;
static int g_tree_last_expanded = -1;
static int g_readme_long;

static uint32_t tree_add(KuiTreeView* tree, int parent, const char* text, const char* icon) {
    uint32_t index = 0;
    if (parent < 0) {
        kui_tree_view_add_node(tree, text, 0, icon, 0, &index);
    } else {
        kui_tree_view_add_child(tree, (uint32_t)parent, text, 0, icon, 0, &index);
    }
    return index;
}

static size_t tree_selected(KuiTreeView* tree, char* out, size_t capacity) {
    uint32_t values[64];
    size_t count = 0;
    kui_tree_view_get_selected(tree, values, 64, &count);
    format_indices(values, count < 64 ? count : 64, out, capacity);
    return count;
}

static int tree_is(KuiTreeView* tree, uint32_t index) {
    int selected = -1;
    kui_tree_view_is_selected(tree, index, &selected);
    return selected;
}

static int32_t tree_index(KuiTreeView* tree) {
    int32_t index = -2;
    kui_tree_view_get_selected_index(tree, &index);
    return index;
}

static uint32_t tree_rows(KuiTreeView* tree) {
    uint32_t count = 0;
    kui_tree_view_get_visible_row_count(tree, &count);
    return count;
}

static int tree_expanded(KuiTreeView* tree, uint32_t index) {
    KuiCTreeNode node;
    memset(&node, 0, sizeof node);
    if (kui_tree_view_get_node(tree, index, &node) != KUI_OK) return -1;
    return node.expanded;
}

static float tree_scroll_top(void) {
    float left = 0, top = -1, width = 0, height = 0;
    KuiElement* box = kuex_by_id(g_doc, "tree-box");
    if (!box) return -1;
    kui_element_get_scroll_metrics(box, &left, &top, &width, &height);
    kui_element_release(box);
    return top;
}

static void show_tree(void) {
    char text[128];
    uint32_t nodes = 0;
    KuiCTreeNode node;
    tree_selected(g_tree, text, sizeof text);
    kuex_set_text(g_doc, "tree-sel", text);
    set_textf("tree-idx", "%d", tree_index(g_tree));
    kui_tree_view_get_node_count(g_tree, &nodes);
    set_textf("tree-rows", "%u", tree_rows(g_tree));
    set_textf("tree-nodes", "%u", nodes);
    memset(&node, 0, sizeof node);
    if (kui_tree_view_get_node(g_tree, N_README, &node) == KUI_OK) {
        snprintf(text, sizeof text, "%.*s", (int)node.text_len, node.text);
        kuex_set_text(g_doc, "tree-readme", text);
    }
    set_textf("tree-scroll", "%.0f", tree_scroll_top());
}

static void on_tree_select(uint32_t index, void* user_data) {
    if (user_data) return; /* the scratch tree */
    g_tree_last_select = index;
    set_textf("tree-event", "on_select(%u)", index);
    show_tree();
}

static void on_tree_toggle(uint32_t index, int expanded, void* user_data) {
    if (user_data) return;
    g_tree_last_toggle = index;
    g_tree_last_expanded = expanded;
    set_textf("tree-event", "on_toggle(%u, %s)", index, expanded ? "expanded" : "collapsed");
    show_tree();
}

static void on_tree_selection(const uint32_t* selected, size_t count, void* user_data) {
    if (user_data) return;
    g_tree_selection_calls++;
    format_indices(selected, count, g_tree_selection, sizeof g_tree_selection);
}

static KuiTreeView* create_tree(const char* id, int multi, void* user_data) {
    KuiCTreeViewConfig config;
    KuiTreeView* tree = NULL;
    KuiElement* container = kuex_by_id(g_doc, id);
    if (!container) return NULL;
    if (kui_tree_view_create(&tree) != KUI_OK) {
        kui_element_release(container);
        return NULL;
    }
    if (multi) {
        tree_add(tree, -1, "src", NULL);
        tree_add(tree, N_SRC, "core", NULL);
        tree_add(tree, N_CORE, "types.h", NULL);
        tree_add(tree, N_CORE, "string.h", NULL);
        tree_add(tree, N_SRC, "ui", NULL);
        tree_add(tree, N_UI, "button.c", NULL);
        tree_add(tree, N_UI, "list.c", NULL);
        tree_add(tree, N_SRC, "main.c", NULL);
        tree_add(tree, -1, "docs", NULL);
        tree_add(tree, N_DOCS, "guide.md", NULL);
        tree_add(tree, N_DOCS, "api.md", NULL);
        tree_add(tree, -1, "README", NULL);
    } else {
        /* Scratch tree: 0 A { 2 C }, 1 B. */
        tree_add(tree, -1, "A", "*");
        tree_add(tree, -1, "B", "*");
        tree_add(tree, 0, "C", "*");
    }
    memset(&config, 0, sizeof config);
    config.item_height = 26.0f;
    config.indent_width = 16.0f;
    config.overscan = 2;
    config.item_bg = "#ffffff";
    config.item_hover_bg = "#f1f5f9";
    config.item_selected_bg = "#16a34a";
    config.item_text_color = "#0f172a";
    config.multi_select = multi;
    config.on_select = on_tree_select;
    config.on_toggle = on_tree_toggle;
    config.on_selection_changed = on_tree_selection;
    config.user_data = user_data;
    kui_tree_view_setup(tree, container, &config);
    kui_element_release(container);
    return tree;
}

static void check_tree_nodes(void) {
    uint32_t count = 0;
    uint32_t child_a = 99, child_b = 99, child_c = 99, child_d = 99;
    KuiStatus missing_child;
    KuiStatus missing_node;
    KuiCTreeNode root;
    KuiCTreeNode core;
    memset(&root, 0, sizeof root);
    memset(&core, 0, sizeof core);

    kui_tree_view_get_node_count(g_tree, &count);
    check("tree.get_node_count", count == N_NODES, "count %u", count);

    kui_tree_view_get_node(g_tree, N_SRC, &root);
    kui_tree_view_get_node(g_tree, N_CORE, &core);
    check("tree.get_node", root.parent == -1 && root.child_count == 3 && core.parent == N_SRC &&
                               core.child_count == 2 && core.expanded == 0 && core.disabled == 0 &&
                               core.text_len == 4 && memcmp(core.text, "core", 4) == 0,
          "root parent %d children %u; core parent %d children %u expanded %d",
          root.parent, (unsigned)root.child_count, core.parent, (unsigned)core.child_count,
          core.expanded);

    kui_tree_view_get_node_child(g_tree, N_CORE, 0, &child_a);
    kui_tree_view_get_node_child(g_tree, N_CORE, 1, &child_b);
    kui_tree_view_get_node_child(g_tree, N_SRC, 2, &child_c);
    missing_child = kui_tree_view_get_node_child(g_tree, N_CORE, 2, &child_d);
    check("tree.get_node_child", child_a == N_TYPES && child_b == N_STRING && child_c == N_MAIN &&
                                     missing_child == KUI_ERR_NOT_FOUND,
          "children %u,%u; src[2] %u; core[2] status %d", child_a, child_b, child_c,
          missing_child);

    missing_node = kui_tree_view_get_node(g_tree, N_NODES, &root);
    check("tree.get_node-missing", missing_node == KUI_ERR_NOT_FOUND, "status %d", missing_node);
}

static void check_tree_expand(void) {
    uint32_t rows = tree_rows(g_tree);
    check("tree.visible-collapsed", rows == 3, "rows %u", rows);

    kui_tree_view_expand(g_tree, N_SRC);
    rows = tree_rows(g_tree);
    check("tree.expand", rows == 6 && tree_expanded(g_tree, N_SRC) == 1 &&
                             g_tree_last_toggle == N_SRC && g_tree_last_expanded == 1,
          "rows %u expanded %d on_toggle(%d,%d)", rows, tree_expanded(g_tree, N_SRC),
          (int)g_tree_last_toggle, g_tree_last_expanded);

    kui_tree_view_expand_all(g_tree);
    rows = tree_rows(g_tree);
    check("tree.expand_all", rows == N_NODES && tree_expanded(g_tree, N_UI) == 1, "rows %u", rows);

    kui_tree_view_collapse(g_tree, N_CORE);
    rows = tree_rows(g_tree);
    check("tree.collapse", rows == 10 && tree_expanded(g_tree, N_CORE) == 0 &&
                               g_tree_last_toggle == N_CORE && g_tree_last_expanded == 0,
          "rows %u on_toggle(%d,%d)", rows, (int)g_tree_last_toggle, g_tree_last_expanded);

    kui_tree_view_toggle(g_tree, N_CORE);
    rows = tree_rows(g_tree);
    kui_tree_view_toggle(g_tree, N_CORE);
    check("tree.toggle", rows == N_NODES && tree_rows(g_tree) == 10 &&
                             tree_expanded(g_tree, N_CORE) == 0,
          "rows after toggles %u, %u", rows, tree_rows(g_tree));

    kui_tree_view_collapse_all(g_tree);
    rows = tree_rows(g_tree);
    check("tree.collapse_all", rows == 3 && tree_expanded(g_tree, N_SRC) == 0 &&
                                   tree_expanded(g_tree, N_UI) == 0 &&
                                   tree_expanded(g_tree, N_DOCS) == 0,
          "rows %u", rows);
}

static void check_tree_selection(void) {
    char text[128];
    size_t count = 99;

    kui_tree_view_expand_all(g_tree);
    kui_tree_view_select(g_tree, N_DOCS);
    tree_selected(g_tree, text, sizeof text);
    check("tree.select", strcmp(text, "[8]") == 0 && tree_index(g_tree) == N_DOCS &&
                             tree_is(g_tree, N_DOCS) == 1 && g_tree_last_select == N_DOCS &&
                             strcmp(g_tree_selection, "[8]") == 0,
          "selected %s index %d on_select %d on_selection_changed %s", text, tree_index(g_tree),
          (int)g_tree_last_select, g_tree_selection);

    kui_tree_view_toggle_select(g_tree, N_README);
    tree_selected(g_tree, text, sizeof text);
    check("tree.toggle_select-add", strcmp(text, "[8,11]") == 0 &&
                                        strcmp(g_tree_selection, "[8,11]") == 0,
          "selected %s", text);
    kui_tree_view_toggle_select(g_tree, N_DOCS);
    tree_selected(g_tree, text, sizeof text);
    check("tree.toggle_select-remove", strcmp(text, "[11]") == 0 && tree_is(g_tree, N_DOCS) == 0,
          "selected %s", text);

    /* select sets the anchor; select_range selects the visible rows from it to the target. */
    kui_tree_view_select(g_tree, N_TYPES);
    kui_tree_view_select_range(g_tree, N_BUTTON);
    tree_selected(g_tree, text, sizeof text);
    check("tree.select_range", strcmp(text, "[2,3,4,5]") == 0 && tree_index(g_tree) == N_BUTTON,
          "selected %s index %d", text, tree_index(g_tree));

    kui_tree_view_select_all(g_tree);
    kui_tree_view_get_selected(g_tree, NULL, 0, &count);
    check("tree.select_all", count == N_NODES && tree_is(g_tree, N_API) == 1, "count %u",
          (unsigned)count);

    kui_tree_view_clear_selection(g_tree);
    count = 99;
    kui_tree_view_get_selected(g_tree, NULL, 0, &count);
    check("tree.clear_selection", count == 0 && tree_index(g_tree) == -1 &&
                                      tree_is(g_tree, N_TYPES) == 0 &&
                                      strcmp(g_tree_selection, "[]") == 0,
          "count %u index %d", (unsigned)count, tree_index(g_tree));
}

/* The scratch tree (single selection): replacing selection, set_node, nodes added after setup. */
static void check_tree_scratch(void) {
    char text[128];
    KuiCTreeNode node;
    uint32_t rows_before;
    uint32_t count = 0;

    kui_tree_view_select(g_scratch_tree, 0);
    kui_tree_view_select(g_scratch_tree, 1);
    tree_selected(g_scratch_tree, text, sizeof text);
    check("tree.select-single", strcmp(text, "[1]") == 0 && tree_is(g_scratch_tree, 0) == 0 &&
                                    tree_index(g_scratch_tree) == 1,
          "selected %s index %d", text, tree_index(g_scratch_tree));

    /* NULL text / icon keep the current value. */
    kui_tree_view_set_node(g_scratch_tree, 2, "C2", 0, NULL, 0, 1);
    kui_tree_view_set_node(g_scratch_tree, 1, NULL, 0, "#", 0, 0);
    memset(&node, 0, sizeof node);
    kui_tree_view_get_node(g_scratch_tree, 2, &node);
    {
        int c2 = node.text_len == 2 && memcmp(node.text, "C2", 2) == 0 && node.icon_len == 1 &&
                 node.icon[0] == '*' && node.disabled == 1;
        memset(&node, 0, sizeof node);
        kui_tree_view_get_node(g_scratch_tree, 1, &node);
        check("tree.set_node", c2 && node.text_len == 1 && node.text[0] == 'B' &&
                                   node.icon_len == 1 && node.icon[0] == '#' && node.disabled == 0,
              "node 2 ok %d; node 1 text %.*s icon %.*s", c2, (int)node.text_len, node.text,
              (int)node.icon_len, node.icon);
    }

    /* kui_c.h: nodes added after setup appear after refresh. */
    kui_tree_view_expand(g_scratch_tree, 0);
    rows_before = tree_rows(g_scratch_tree);
    tree_add(g_scratch_tree, 0, "D", NULL);
    kui_tree_view_get_node_count(g_scratch_tree, &count);
    {
        uint32_t rows_added = tree_rows(g_scratch_tree);
        kui_tree_view_refresh(g_scratch_tree);
        check("tree.refresh", rows_before == 3 && rows_added == 3 && count == 4 &&
                                  tree_rows(g_scratch_tree) == 4 && text_has("scratch-tree", "C2") &&
                                  text_has("scratch-tree", "D"),
              "rows %u -> %u -> %u, nodes %u", rows_before, rows_added, tree_rows(g_scratch_tree),
              count);
    }
}

/* Panel buttons. */
static void on_tree_expand_all(KuiEvent* e, void* u) { (void)e; (void)u; kui_tree_view_expand_all(g_tree); show_tree(); }
static void on_tree_collapse_all(KuiEvent* e, void* u) { (void)e; (void)u; kui_tree_view_collapse_all(g_tree); show_tree(); }
static void on_tree_toggle_src(KuiEvent* e, void* u) { (void)e; (void)u; kui_tree_view_toggle(g_tree, N_SRC); show_tree(); }
static void on_tree_collapse_core(KuiEvent* e, void* u) { (void)e; (void)u; kui_tree_view_collapse(g_tree, N_CORE); show_tree(); }
static void on_tree_select_core(KuiEvent* e, void* u) { (void)e; (void)u; kui_tree_view_select(g_tree, N_CORE); show_tree(); }
static void on_tree_toggle_docs(KuiEvent* e, void* u) { (void)e; (void)u; kui_tree_view_toggle_select(g_tree, N_DOCS); show_tree(); }
static void on_tree_range_main(KuiEvent* e, void* u) { (void)e; (void)u; kui_tree_view_select_range(g_tree, N_MAIN); show_tree(); }
static void on_tree_all(KuiEvent* e, void* u) { (void)e; (void)u; kui_tree_view_select_all(g_tree); show_tree(); }
static void on_tree_clear(KuiEvent* e, void* u) { (void)e; (void)u; kui_tree_view_clear_selection(g_tree); show_tree(); }
static void on_tree_scroll(KuiEvent* e, void* u) { (void)e; (void)u; kui_tree_view_scroll_to_node(g_tree, N_README); show_tree(); }

static void on_tree_rename(KuiEvent* e, void* u) {
    (void)e; (void)u;
    g_readme_long = !g_readme_long;
    kui_tree_view_set_node(g_tree, N_README, g_readme_long ? "README.md" : "README", 0, NULL, 0, 0);
    kui_tree_view_refresh(g_tree);
    show_tree();
}

/* ======================================================================================== */
/* ---- VirtualList: focus, insert / remove, total count, scrolling ----------------------- */

#define VL_COUNT 1000u
#define VL_ROW 24.0f
#define VL_OVERSCAN 3u
#define VL_CAPACITY 4096u
#define VL_NEW_BASE 1000000u   /* ids at or above are inserted rows ("New n") */

static KuiVirtualList* g_vlist;
static uint32_t g_vl_items[VL_CAPACITY];   /* the data model: one id per row */
static uint32_t g_vl_count;
static uint32_t g_vl_next_new = 1;
static int g_vl_generation;
static int g_vl_rendered;                  /* render_item calls */
static int32_t g_vl_focus = -1;            /* last on_focus_change */
static int32_t g_vl_focus_old = -2;
static uint32_t g_vl_total = VL_COUNT;     /* set_total_items button */

static void vl_reset_model(void) {
    uint32_t i;
    for (i = 0; i < VL_COUNT; ++i) g_vl_items[i] = i;
    g_vl_count = VL_COUNT;
    g_vl_next_new = 1;
}

static void vl_label(uint32_t index, char* out, size_t capacity) {
    uint32_t id = index < g_vl_count ? g_vl_items[index] : index;
    if (id >= VL_NEW_BASE) {
        snprintf(out, capacity, "New %u", id - VL_NEW_BASE);
    } else {
        snprintf(out, capacity, "Item %u", id);
    }
    if (g_vl_generation > 0) {
        size_t used = strlen(out);
        snprintf(out + used, capacity - used, " r%d", g_vl_generation);
    }
}

static void vl_model_insert(uint32_t index, uint32_t count) {
    uint32_t i;
    if (g_vl_count + count > VL_CAPACITY || index > g_vl_count) return;
    memmove(g_vl_items + index + count, g_vl_items + index,
            (g_vl_count - index) * sizeof g_vl_items[0]);
    for (i = 0; i < count; ++i) g_vl_items[index + i] = VL_NEW_BASE + g_vl_next_new++;
    g_vl_count += count;
}

static void vl_model_remove(uint32_t index, uint32_t count) {
    if (index + count > g_vl_count) return;
    memmove(g_vl_items + index, g_vl_items + index + count,
            (g_vl_count - index - count) * sizeof g_vl_items[0]);
    g_vl_count -= count;
}

static KuiElement* vl_render(KuiDocument* document, uint32_t index, void* user_data) {
    char text[48];
    const char* styles[] = {"display", "flex", "align-items", "center", "padding", "0 10px",
                            "font-size", "12px", "color", "#0f172a",
                            "background", (int32_t)index == g_vl_focus ? "#f59e0b" : "#ffffff",
                            NULL};
    (void)user_data;
    g_vl_rendered++;
    vl_label(index, text, sizeof text);
    return kuex_make_element(document, "div", text, styles);
}

static KuiCVirtualListState vl_state(void) {
    KuiCVirtualListState state;
    memset(&state, 0, sizeof state);
    kui_virtual_list_get_state(g_vlist, &state);
    return state;
}

static void show_vlist(void) {
    char text[48];
    KuiCVirtualListState state = vl_state();
    set_textf("vl-total", "%u", state.total_items);
    set_textf("vl-focus", "%d", state.focused_index);
    set_textf("vl-start", "%u", state.visible_start);
    set_textf("vl-end", "%u", state.visible_end);
    vl_label(0, text, sizeof text);
    kuex_set_text(g_doc, "vl-first", text);
}

static void on_vl_focus_change(int32_t old_index, int32_t new_index, void* user_data) {
    (void)user_data;
    g_vl_focus_old = old_index;
    g_vl_focus = new_index;
    set_textf("vl-event", "focus %d -> %d", old_index, new_index);
}

static void on_vl_activate(uint32_t index, void* user_data) {
    (void)user_data;
    set_textf("vl-event", "on_activate(%u)", index);
}

static KuiVirtualList* create_vlist(void) {
    KuiCVirtualListConfig config;
    KuiVirtualList* list = NULL;
    KuiElement* container = kuex_by_id(g_doc, "vl-box");
    if (!container) return NULL;
    vl_reset_model();
    memset(&config, 0, sizeof config);
    config.total_items = VL_COUNT;
    config.item_height = VL_ROW;
    config.overscan = VL_OVERSCAN;
    config.enable_keyboard = 1;
    config.render_item = vl_render;
    config.on_focus_change = on_vl_focus_change;
    config.on_activate = on_vl_activate;
    kui_virtual_list_create(container, &config, &list);
    kui_element_release(container);
    return list;
}

static void check_vlist(void) {
    KuiCVirtualListState state = vl_state();
    int before;

    check("vlist.get_state", g_vlist && state.total_items == VL_COUNT &&
                                 state.item_height == VL_ROW && state.total_height == 24000.0f &&
                                 state.focused_index == -1 && state.visible_start == 0 &&
                                 state.visible_end > 0 && state.visible_end <= VL_COUNT &&
                                 g_vl_rendered > 0 && text_has("vl-box", "Item 0"),
          "total %u row %.0f height %.0f focus %d rendered %u-%u", state.total_items,
          state.item_height, state.total_height, state.focused_index, state.visible_start,
          state.visible_end);

    kui_virtual_list_scroll_to_index(g_vlist, 500);
    state = vl_state();
    check("vlist.scroll_to_index", state.visible_start <= 500 && state.visible_end > 500 &&
                                       text_has("vl-box", "Item 500"),
          "rendered %u-%u", state.visible_start, state.visible_end);

    kui_virtual_list_scroll_to_bottom(g_vlist);
    state = vl_state();
    check("vlist.scroll_to_bottom", state.visible_end == VL_COUNT &&
                                        text_has("vl-box", "Item 999"),
          "rendered %u-%u", state.visible_start, state.visible_end);

    kui_virtual_list_scroll_to_top(g_vlist);
    state = vl_state();
    check("vlist.scroll_to_top", state.visible_start == 0 && text_has("vl-box", "Item 0"),
          "rendered %u-%u", state.visible_start, state.visible_end);

    /* kui_c.h: set_focused_index calls on_focus_change when it changes. */
    kui_virtual_list_set_focused_index(g_vlist, 10);
    state = vl_state();
    check("vlist.set_focused_index", state.focused_index == 10 && g_vl_focus_old == -1 &&
                                         g_vl_focus == 10 && state.visible_start <= 10 &&
                                         state.visible_end > 10,
          "focus %d event %d->%d rendered %u-%u", state.focused_index, g_vl_focus_old,
          g_vl_focus, state.visible_start, state.visible_end);

    /* kui_c.h: clamped to the rows; -1 clears. */
    kui_virtual_list_set_focused_index(g_vlist, 5000);
    state = vl_state();
    {
        int32_t clamped = state.focused_index;
        int32_t event_old = g_vl_focus_old;
        kui_virtual_list_set_focused_index(g_vlist, -1);
        state = vl_state();
        check("vlist.set_focused_index-clamp", clamped == 999 && event_old == 10 &&
                                                   state.focused_index == -1 &&
                                                   g_vl_focus_old == 999 && g_vl_focus == -1,
              "5000 -> %d, -1 -> %d (event %d->%d)", clamped, state.focused_index,
              g_vl_focus_old, g_vl_focus);
    }

    kui_virtual_list_set_focused_index(g_vlist, 10);
    kui_virtual_list_move_focus(g_vlist, 5);
    state = vl_state();
    {
        int32_t moved = state.focused_index;
        kui_virtual_list_move_focus(g_vlist, -100);
        state = vl_state();
        check("vlist.move_focus", moved == 15 && state.focused_index == 0 && g_vl_focus == 0,
              "10+5 -> %d, -100 -> %d", moved, state.focused_index);
    }
    kui_virtual_list_set_focused_index(g_vlist, -1);

    /* Insert three rows at the top, then remove them again. */
    kui_virtual_list_scroll_to_top(g_vlist);
    vl_model_insert(0, 3);
    kui_virtual_list_insert_items(g_vlist, 0, 3);
    kui_virtual_list_scroll_to_top(g_vlist);
    state = vl_state();
    check("vlist.insert_items", state.total_items == VL_COUNT + 3 &&
                                    state.total_height == 24072.0f && text_has("vl-box", "New 1"),
          "total %u height %.0f", state.total_items, state.total_height);

    vl_model_remove(0, 3);
    kui_virtual_list_remove_items(g_vlist, 0, 3);
    state = vl_state();
    check("vlist.remove_items", state.total_items == VL_COUNT && !text_has("vl-box", "New 1") &&
                                    text_has("vl-box", "Item 0"),
          "total %u", state.total_items);

    kui_virtual_list_set_total_items(g_vlist, 50);
    state = vl_state();
    check("vlist.set_total_items", state.total_items == 50 && state.total_height == 1200.0f &&
                                       state.visible_end <= 50,
          "total %u height %.0f rendered %u-%u", state.total_items, state.total_height,
          state.visible_start, state.visible_end);
    kui_virtual_list_set_total_items(g_vlist, VL_COUNT);

    before = g_vl_rendered;
    g_vl_generation = 1;
    kui_virtual_list_refresh(g_vlist);
    check("vlist.refresh", g_vl_rendered > before && text_has("vl-box", "Item 0 r1"),
          "render calls %d -> %d", before, g_vl_rendered);

    /* Removing the rows around the focused row: the focus must stay within the rows. */
    kui_virtual_list_set_focused_index(g_vlist, 999);
    vl_model_remove(990, 10);
    kui_virtual_list_remove_items(g_vlist, 990, 10);
    state = vl_state();
    known("vlist.remove_items-focus", state.total_items == 990 && state.focused_index < 990,
          "remove_items leaves focused_index past the last row", "total %u focus %d",
          state.total_items, state.focused_index);

    /* Back to the starting state. */
    vl_reset_model();
    kui_virtual_list_set_total_items(g_vlist, VL_COUNT);
    kui_virtual_list_set_focused_index(g_vlist, -1);
    kui_virtual_list_scroll_to_top(g_vlist);
    kui_virtual_list_refresh(g_vlist);
}

/* Panel buttons. */
static void vl_after_focus(void) {
    kui_virtual_list_refresh(g_vlist); /* repaint the amber focus row */
    show_vlist();
}

static void on_vl_focus10(KuiEvent* e, void* u) { (void)e; (void)u; kui_virtual_list_set_focused_index(g_vlist, 10); vl_after_focus(); }
static void on_vl_prev(KuiEvent* e, void* u) { (void)e; (void)u; kui_virtual_list_move_focus(g_vlist, -1); vl_after_focus(); }
static void on_vl_next(KuiEvent* e, void* u) { (void)e; (void)u; kui_virtual_list_move_focus(g_vlist, 1); vl_after_focus(); }

static void on_vl_insert(KuiEvent* e, void* u) {
    (void)e; (void)u;
    vl_model_insert(0, 3);
    kui_virtual_list_insert_items(g_vlist, 0, 3);
    show_vlist();
}

static void on_vl_remove(KuiEvent* e, void* u) {
    (void)e; (void)u;
    if (g_vl_count < 3) return;
    vl_model_remove(0, 3);
    kui_virtual_list_remove_items(g_vlist, 0, 3);
    show_vlist();
}

static void on_vl_goto(KuiEvent* e, void* u) { (void)e; (void)u; kui_virtual_list_scroll_to_index(g_vlist, 500); show_vlist(); }
static void on_vl_top(KuiEvent* e, void* u) { (void)e; (void)u; kui_virtual_list_scroll_to_top(g_vlist); show_vlist(); }
static void on_vl_bottom(KuiEvent* e, void* u) { (void)e; (void)u; kui_virtual_list_scroll_to_bottom(g_vlist); show_vlist(); }

static void on_vl_total(KuiEvent* e, void* u) {
    (void)e; (void)u;
    g_vl_total = g_vl_total == 50u ? g_vl_count : 50u;
    kui_virtual_list_set_total_items(g_vlist, g_vl_total);
    show_vlist();
}

static void on_vl_refresh(KuiEvent* e, void* u) {
    (void)e; (void)u;
    g_vl_generation++;
    kui_virtual_list_refresh(g_vlist);
    set_textf("vl-event", "refresh r%d", g_vl_generation);
    show_vlist();
}

/* ======================================================================================== */
/* ---- Deferred phase: scrolling the tree needs a laid-out frame, then the verdict -------- */

static void phase_tree_scrolled(void* user_data) {
    float scrolled;
    float back;
    (void)user_data;
    scrolled = tree_scroll_top();
    kui_tree_view_scroll_to_node(g_tree, N_SRC);
    back = tree_scroll_top();
    check("tree.scroll_to_node", scrolled > 0.0f && back == 0.0f, "README -> %.0f, src -> %.0f",
          scrolled, back);

    /* Back to the starting state. */
    kui_tree_view_collapse_all(g_tree);
    kui_tree_view_clear_selection(g_tree);
    show_tree();
    show_list();
    show_vlist();
    kuex_set_text(g_doc, "lv-event", "-");
    kuex_set_text(g_doc, "tree-event", "-");
    kuex_set_text(g_doc, "vl-event", "-");
    finish();
}

static void phase_tree_scroll(void* user_data) {
    KuiTimerId id;
    (void)user_data;
    kui_tree_view_scroll_to_node(g_tree, N_README); /* the tree is fully expanded here */
    kui_platform_set_timeout(phase_tree_scrolled, NULL, 150, &id);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    int failed = 0;
    KuiTimerId id;
    g_doc = host->document;

    g_single = create_list("lv-single", KUI_C_SELECTION_SINGLE, "single");
    g_multi = create_list("lv-multi", KUI_C_SELECTION_MULTI, "multi");
    g_tree = create_tree("tree-box", 1, NULL);
    g_scratch_tree = create_tree("scratch-tree", 0, (void*)"scratch");
    g_vlist = create_vlist();
    if (!g_single || !g_multi || !g_tree || !g_scratch_tree || !g_vlist) return 1;

    check_list_view();
    check_tree_nodes();
    check_tree_expand();
    check_tree_selection();
    check_tree_scratch();
    check_vlist();
    kui_platform_set_timeout(phase_tree_scroll, NULL, 150, &id);

    failed |= kuex_on(g_doc, "btn-lv-sel2", "click", on_lv_select2, NULL);
    failed |= kuex_on(g_doc, "btn-lv-sel4", "click", on_lv_select4, NULL);
    failed |= kuex_on(g_doc, "btn-lv-range", "click", on_lv_range, NULL);
    failed |= kuex_on(g_doc, "btn-lv-desel", "click", on_lv_deselect, NULL);
    failed |= kuex_on(g_doc, "btn-lv-clear", "click", on_lv_clear, NULL);
    failed |= kuex_on(g_doc, "btn-lv-goto", "click", on_lv_goto, NULL);
    failed |= kuex_on(g_doc, "btn-lv-top", "click", on_lv_top, NULL);
    failed |= kuex_on(g_doc, "btn-lv-bottom", "click", on_lv_bottom, NULL);
    failed |= kuex_on(g_doc, "btn-lv-count", "click", on_lv_count, NULL);
    failed |= kuex_on(g_doc, "btn-lv-refresh", "click", on_lv_refresh, NULL);

    failed |= kuex_on(g_doc, "btn-tree-expand-all", "click", on_tree_expand_all, NULL);
    failed |= kuex_on(g_doc, "btn-tree-collapse-all", "click", on_tree_collapse_all, NULL);
    failed |= kuex_on(g_doc, "btn-tree-toggle", "click", on_tree_toggle_src, NULL);
    failed |= kuex_on(g_doc, "btn-tree-collapse", "click", on_tree_collapse_core, NULL);
    failed |= kuex_on(g_doc, "btn-tree-select", "click", on_tree_select_core, NULL);
    failed |= kuex_on(g_doc, "btn-tree-toggle-sel", "click", on_tree_toggle_docs, NULL);
    failed |= kuex_on(g_doc, "btn-tree-range", "click", on_tree_range_main, NULL);
    failed |= kuex_on(g_doc, "btn-tree-all", "click", on_tree_all, NULL);
    failed |= kuex_on(g_doc, "btn-tree-clear", "click", on_tree_clear, NULL);
    failed |= kuex_on(g_doc, "btn-tree-scroll", "click", on_tree_scroll, NULL);
    failed |= kuex_on(g_doc, "btn-tree-rename", "click", on_tree_rename, NULL);

    failed |= kuex_on(g_doc, "btn-vl-focus", "click", on_vl_focus10, NULL);
    failed |= kuex_on(g_doc, "btn-vl-prev", "click", on_vl_prev, NULL);
    failed |= kuex_on(g_doc, "btn-vl-next", "click", on_vl_next, NULL);
    failed |= kuex_on(g_doc, "btn-vl-insert", "click", on_vl_insert, NULL);
    failed |= kuex_on(g_doc, "btn-vl-remove", "click", on_vl_remove, NULL);
    failed |= kuex_on(g_doc, "btn-vl-goto", "click", on_vl_goto, NULL);
    failed |= kuex_on(g_doc, "btn-vl-top", "click", on_vl_top, NULL);
    failed |= kuex_on(g_doc, "btn-vl-bottom", "click", on_vl_bottom, NULL);
    failed |= kuex_on(g_doc, "btn-vl-total", "click", on_vl_total, NULL);
    failed |= kuex_on(g_doc, "btn-vl-refresh", "click", on_vl_refresh, NULL);
    return failed;
}
