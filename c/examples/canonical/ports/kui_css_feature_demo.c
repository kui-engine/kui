/* Host logic of examples/css_feature_demo/main.cpp: builds the "C++ Dynamic DOM" section into
 * #dynamic-section through the DOM API, updates #status-text, and binds the sidebar nav items
 * (active class + scroll the section into view). The original loads css_feature_test.html
 * itself; the runner loads that same file as this example's page.
 * The nav items scroll their section into view with kui_element_scroll_into_view. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;

/* ---- C++ Dynamic DOM section ------------------------------------------------------------ */

#define SECTION_HEADING_STYLES \
    "font-size", "12px", "color", "var(--c-dim)", "text-transform", "uppercase", \
    "letter-spacing", "1px", "margin-bottom", "6px", NULL

static void add_heading(KuiElement* section, const char* text) {
    const char* styles[] = {SECTION_HEADING_STYLES};
    kuex_append_release(section, kuex_make_element(g_doc, "div", text, styles));
}

static void build_palette(KuiElement* section) {
    const char* row_styles[] = {"display", "flex", "gap", "4px", "flex-wrap", "wrap",
                                "margin-bottom", "12px", NULL};
    KuiElement* row;
    int i;
    add_heading(section, "1. Dynamic Color Palette (C++ generated)");
    row = kuex_make_element(g_doc, "div", NULL, row_styles);
    for (i = 0; i < 16; i++) {
        char color[32];
        char label[8];
        const char* styles[] = {
            "width", "36px", "height", "36px", "background", color, "border-radius", "6px",
            "display", "flex", "align-items", "center", "justify-content", "center",
            "font-size", "9px", "color", "white", "font-weight", "700",
            "transition", "transform 0.2s ease, box-shadow 0.2s", "cursor", "pointer", NULL};
        snprintf(color, sizeof color, "hsl(%d, 70%%, 55%%)", i * 22);
        snprintf(label, sizeof label, "%d", i * 22);
        kuex_append_release(row, kuex_make_element(g_doc, "div", label, styles));
    }
    kuex_append_release(section, row);
}

static void build_flex_bars(KuiElement* section) {
    static const char* const labels[] = {"CSS 2.1", "Selectors", "Flexbox", "Grid",
                                         "Transforms", "Animations", "Colors"};
    static const int values[] = {98, 85, 100, 95, 100, 100, 88};
    static const char* const colors[] = {"#e17055", "#fdcb6e", "#00b894", "#0984e3",
                                         "#6c5ce7", "#a29bfe", "#fd79a8"};
    const char* row_styles[] = {"display", "flex", "gap", "3px", "height", "30px",
                                "margin-bottom", "12px", NULL};
    KuiElement* row;
    int i;
    add_heading(section, "2. Dynamic Flex Layout (proportional bars)");
    row = kuex_make_element(g_doc, "div", NULL, row_styles);
    for (i = 0; i < 7; i++) {
        char grow[8];
        const char* styles[] = {
            "flex-grow", grow, "background", colors[i], "border-radius", "4px",
            "display", "flex", "align-items", "center", "justify-content", "center",
            "font-size", "9px", "color", "white", "font-weight", "700",
            "overflow", "hidden", "white-space", "nowrap",
            "transition", "flex-grow 0.5s ease", NULL};
        snprintf(grow, sizeof grow, "%d", values[i]);
        kuex_append_release(row, kuex_make_element(g_doc, "div", labels[i], styles));
    }
    kuex_append_release(section, row);
}

static void build_grid_dashboard(KuiElement* section) {
    static const char* const stats[4][4] = {
        {"CSS Properties", "307", "#6c5ce7", "+12"},
        {"Selectors", "44/52", "#00b894", "85%"},
        {"Layout Tests", "100%", "#0984e3", "Flex+Grid"},
        {"Animations", "11/11", "#fd79a8", "100%"},
    };
    const char* grid_styles[] = {"display", "grid", "grid-template-columns", "repeat(4, 1fr)",
                                 "gap", "8px", "margin-bottom", "12px", NULL};
    KuiElement* grid;
    int i;
    add_heading(section, "3. Dynamic Grid Dashboard (C++ data binding)");
    grid = kuex_make_element(g_doc, "div", NULL, grid_styles);
    for (i = 0; i < 4; i++) {
        char border[48];
        const char* card_styles[] = {"background", "var(--c-card)", "border-radius", "8px",
                                     "padding", "12px", "border-left", border, NULL};
        const char* label_styles[] = {"font-size", "10px", "color", "var(--c-dim)",
                                      "margin-bottom", "4px", NULL};
        const char* value_styles[] = {"font-size", "20px", "font-weight", "800",
                                      "color", stats[i][2], NULL};
        const char* trend_styles[] = {"font-size", "10px", "color", "var(--c-dim)",
                                      "margin-top", "2px", NULL};
        KuiElement* card;
        snprintf(border, sizeof border, "3px solid %s", stats[i][2]);
        card = kuex_make_element(g_doc, "div", NULL, card_styles);
        kuex_append_release(card, kuex_make_element(g_doc, "div", stats[i][0], label_styles));
        kuex_append_release(card, kuex_make_element(g_doc, "div", stats[i][1], value_styles));
        kuex_append_release(card, kuex_make_element(g_doc, "div", stats[i][3], trend_styles));
        kuex_append_release(grid, card);
    }
    kuex_append_release(section, grid);
}

static void build_style_demos(KuiElement* section) {
    static const char* const circle[] = {
        "width", "40px", "height", "40px", "background", "var(--c-primary)",
        "border-radius", "50%", "display", "flex", "align-items", "center",
        "justify-content", "center", "color", "white", "font-size", "9px", NULL};
    static const char* const shadow[] = {
        "padding", "8px", "background", "var(--c-card)", "border-radius", "6px",
        "box-shadow", "0 4px 12px rgba(108,92,231,0.4)", "font-size", "10px",
        "color", "var(--c-dim)", NULL};
    static const char* const gradient[] = {
        "padding", "8px", "background", "linear-gradient(135deg, #6c5ce7, #fd79a8)",
        "border-radius", "6px", "color", "white", "font-size", "10px", NULL};
    static const char* const transform[] = {
        "padding", "8px", "background", "var(--c-card)", "border-radius", "6px",
        "transform", "rotate(5deg) scale(1.05)", "font-size", "10px",
        "color", "var(--c-dim)", NULL};
    static const char* const opacity[] = {
        "padding", "8px", "background", "var(--c-accent)", "border-radius", "6px",
        "opacity", "0.5", "color", "white", "font-size", "10px", NULL};
    static const char* const blur[] = {
        "padding", "8px", "background", "var(--c-success)", "border-radius", "6px",
        "filter", "blur(1px)", "color", "white", "font-size", "10px", NULL};
    static const char* const text_shadow[] = {
        "padding", "8px", "background", "var(--c-card)", "border-radius", "6px",
        "text-shadow", "0 0 8px var(--c-primary)", "font-size", "10px",
        "color", "var(--c-secondary)", NULL};
    static const char* const outline[] = {
        "padding", "8px", "background", "var(--c-card)", "border-radius", "6px",
        "outline", "2px solid var(--c-accent)", "outline-offset", "3px",
        "font-size", "10px", "color", "var(--c-dim)", NULL};
    static const struct { const char* label; const char* const* styles; } demos[] = {
        {"border-radius: 50%", circle}, {"box-shadow", shadow}, {"gradient bg", gradient},
        {"transform", transform}, {"opacity: 0.5", opacity}, {"filter: blur", blur},
        {"text-shadow", text_shadow}, {"outline", outline},
    };
    const char* row_styles[] = {"display", "flex", "gap", "6px", "flex-wrap", "wrap",
                                "margin-bottom", "12px", NULL};
    KuiElement* row;
    size_t i;
    add_heading(section, "4. Programmatic Style Manipulation");
    row = kuex_make_element(g_doc, "div", NULL, row_styles);
    for (i = 0; i < sizeof demos / sizeof demos[0]; i++) {
        kuex_append_release(row, kuex_make_element(g_doc, "div", demos[i].label, demos[i].styles));
    }
    kuex_append_release(section, row);
}

static void build_nested_layout(KuiElement* section) {
    static const char* const cell_colors[] = {"var(--c-danger)", "var(--c-success)",
                                              "var(--c-primary)", "var(--c-warning)"};
    static const char* const cell_labels[] = {"Errors: 0", "Tests: 730", "Coverage: 91%",
                                              "Build: OK"};
    const char* layout_styles[] = {"display", "flex", "gap", "8px", "height", "100px",
                                   "margin-bottom", "12px", NULL};
    const char* sidebar_styles[] = {"width", "60px", "background", "var(--c-card)",
                                    "border-radius", "6px", "padding", "6px", "display", "flex",
                                    "flex-direction", "column", "gap", "4px", NULL};
    const char* grid_styles[] = {"flex", "1", "display", "grid",
                                 "grid-template-columns", "1fr 1fr",
                                 "grid-template-rows", "1fr 1fr", "gap", "4px", NULL};
    KuiElement* layout;
    KuiElement* mini_sidebar;
    KuiElement* content_grid;
    int i;
    add_heading(section, "5. Complex Nested Layout (sidebar + grid + flex)");
    layout = kuex_make_element(g_doc, "div", NULL, layout_styles);
    mini_sidebar = kuex_make_element(g_doc, "div", NULL, sidebar_styles);
    for (i = 0; i < 4; i++) {
        char label[4];
        const char* styles[] = {
            "background", i == 0 ? "var(--c-primary)" : "var(--c-border)",
            "border-radius", "4px", "padding", "2px", "text-align", "center",
            "font-size", "10px", "color", i == 0 ? "white" : "var(--c-dim)", NULL};
        snprintf(label, sizeof label, "%d", i + 1);
        kuex_append_release(mini_sidebar, kuex_make_element(g_doc, "div", label, styles));
    }
    kuex_append_release(layout, mini_sidebar);
    content_grid = kuex_make_element(g_doc, "div", NULL, grid_styles);
    for (i = 0; i < 4; i++) {
        const char* styles[] = {
            "background", cell_colors[i], "border-radius", "6px", "display", "flex",
            "align-items", "center", "justify-content", "center", "font-size", "10px",
            "color", "white", "font-weight", "700", NULL};
        kuex_append_release(content_grid, kuex_make_element(g_doc, "div", cell_labels[i], styles));
    }
    kuex_append_release(layout, content_grid);
    kuex_append_release(section, layout);
}

static void build_dynamic_section(void) {
    const char* section_styles[] = {"background", "var(--c-surface)",
                                    "border", "1px solid var(--c-border)",
                                    "border-radius", "var(--radius)", "padding", "16px",
                                    "margin-bottom", "16px", NULL};
    const char* title_styles[] = {"font-size", "15px", "font-weight", "700",
                                  "color", "var(--c-accent)", "margin-bottom", "10px",
                                  "display", "flex", "align-items", "center", "gap", "8px", NULL};
    const char* subtitle_styles[] = {"font-size", "11px", "color", "var(--c-dim)",
                                     "margin-bottom", "12px", NULL};
    KuiElement* container = kuex_by_id(g_doc, "dynamic-section");
    KuiElement* section;
    if (!container) return;
    section = kuex_make_element(g_doc, "div", NULL, section_styles);
    kuex_append_release(section, kuex_make_element(g_doc, "div", "C++ Dynamic DOM Manipulation",
                                                   title_styles));
    kuex_append_release(section, kuex_make_element(
        g_doc, "div", "The following elements are created entirely via C++ API.", subtitle_styles));
    build_palette(section);
    build_flex_bars(section);
    build_grid_dashboard(section);
    build_style_demos(section);
    build_nested_layout(section);
    kuex_append_release(container, section);
    kui_element_release(container);

    kuex_set_text(g_doc, "status-text", "Loaded: 16 sections, 307+ CSS properties tested");
}

/* ---- Sidebar nav: nav-id -> section-id -------------------------------------------------- */

static const char* const k_nav[][2] = {
    {"nav-1", "sec-1"},   {"nav-2", "sec-2"},   {"nav-3", "sec-3"},   {"nav-4", "sec-4"},
    {"nav-5", "sec-5"},   {"nav-7", "sec-7"},   {"nav-8", "sec-8"},   {"nav-9", "sec-9"},
    {"nav-10", "sec-10"}, {"nav-11", "sec-11"}, {"nav-13", "sec-13"}, {"nav-14", "sec-14"},
    {"nav-15", "sec-15"}, {"nav-16", "sec-16"}, {"nav-cpp", "dynamic-section"},
};
#define NAV_COUNT (sizeof k_nav / sizeof k_nav[0])

static void on_nav(KuiEvent* event, void* user_data) {
    size_t index = (size_t)(uintptr_t)user_data;
    size_t i;
    (void)event;
    /* Update active state on all nav items */
    for (i = 0; i < NAV_COUNT; ++i) {
        KuiElement* nav = kuex_by_id(g_doc, k_nav[i][0]);
        if (!nav) continue;
        kui_element_set_attribute(nav, "class", 0, "nav-item", 0);
        kui_element_release(nav);
    }
    {
        KuiElement* nav = kuex_by_id(g_doc, k_nav[index][0]);
        if (nav) {
            kui_element_set_attribute(nav, "class", 0, "nav-item active", 0);
            kui_element_release(nav);
        }
    }
    /* Scroll the section into view */
    {
        KuiElement* section = kuex_by_id(g_doc, k_nav[index][1]);
        if (section) {
            kui_element_scroll_into_view(section, 1);
            kui_element_release(section);
        }
    }
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    size_t i;
    g_doc = host->document;
    kui_document_register_builtin_components(g_doc); /* before loadHTML in the original */

    /* Build the dynamic C++ section after HTML is loaded */
    build_dynamic_section();

    /* Bind sidebar nav-item click events (pairs whose nav or section is missing are skipped) */
    for (i = 0; i < NAV_COUNT; ++i) {
        KuiElement* nav = kuex_by_id(g_doc, k_nav[i][0]);
        KuiElement* sec = kuex_by_id(g_doc, k_nav[i][1]);
        if (nav && sec) {
            kui_element_add_event_listener(nav, "click", 0, on_nav, (void*)(uintptr_t)i, NULL);
        }
        if (nav) kui_element_release(nav);
        if (sec) kui_element_release(sec);
    }
    fprintf(stderr, "[CSS Demo] Running \xE2\x80\x94 16 CSS categories, 332 CSS properties\n");
    return 0;
}
