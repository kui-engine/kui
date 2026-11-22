/* Host logic of examples/html_loader/main.cpp (kui_html_loader, the baseline variant): scans the
 * sample folder, loads the first sample with the navigation bar injected after <body>, titles the
 * window after it, and wires the bar: the sample <select> switches pages, "Open File..." loads any
 * HTML file, "Inspect" toggles its own highlight. Each load gets a fresh script engine and the
 * virtual_components sample gets its VirtualList / ListView / TreeView and scroll-unit toggle.
 * The four kui_html_loader* ports are the same file except g_variant.
 * prepare sets the variant's renderer presets (KUI_ENABLE_RETAINED_SURFACE /
 * KUI_ENABLE_LAYER_PROMOTION, only when unset: the user's environment wins) before any KUI call,
 * handles --help / --list and parses the command line as the original (sample name substring,
 * --file, --dev, --devtools [port], --devtools-port, --size, --frameless, --shadow, --radius,
 * --sizeable, --show-after-load, --dpr).
 *
 * Differences from the original, all forced by the C ABI or the runner:
 * - Window shape: the runner creates the window from KuiWindowProps, so --radius and --sizeable
 *   are applied right after creation (before_load), and --show-after-load hides the window there,
 *   renders the first frame with kui_window_process_frame and shows it once the page is loaded.
 * - --dev and the script engine's console: kui_console_*; --no-token: kui_window_set_devtools_token_auth;
 *   --dpr: kui_document_set_render_device_pixel_ratio. --devtools uses the window's DevTools server
 *   (kui_window_start_devtools) and is skipped when it already runs.
 * - The runner loads the packaged "No samples found" page first; the port replaces it.
 * - Base path: kui_document_set_base_path.
 * - Sample switching listens to the DOM "change" event and reads selectedIndex through Lua; the
 *   page is replaced from a zero-delay timer, because a listener must not destroy the tree that
 *   is dispatching it (the original's onChange runs after dispatch).
 * - "Inspect" only toggles the button style: there is no ABI for EventDispatcher's inspector mode
 *   or inspect callback (DevTools on demand, launchBrowser, pushInspectElement).
 * - Open dialog filters carry no labels. The stack trace writer and the crash reporter are installed in
 *   prepare, as the original does before Platform::init.
 * - Samples are also searched in <exe>/assets/native_samples, where the C package puts them. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "kui_c_example_dom.h"

#include <ctype.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

static const int g_variant = 0; /* 1 = retained surface, 2 = layer promotion, 3 = both */
#define PATH_CAP 4096

typedef struct Sample {
    char name[256];
    char filename[256];
    char path[PATH_CAP];
} Sample;

static Sample* g_samples;
static int g_count;
static int g_current;
static KuiWindow* g_window;
static KuiDocument* g_doc;
static KuiScriptEngine* g_runner_script; /* the runner's engine, unbound at the first load */
static KuiScriptEngine* g_engine;        /* recreated on every load, like setupScriptEngine */
static KuiVirtualList* g_vlist;
static KuiListView* g_list;
static KuiTreeView* g_tree;
static int g_inspect;
static int g_pending_index = -1;
static char g_pending_path[PATH_CAP];

/* Command line (main() of the original). */
static const char* g_file_to_load;
static int g_devtools;
static int g_devtools_port = 9229;
static int g_sizeable;
static int g_show_after_load;
static int g_has_radius;
static float g_radius;
static int g_dev_mode;
static int g_no_token;
static float g_backend_dpr;

/* -- growable string ------------------------------------------------------------------------ */
typedef struct Buf {
    char* data;
    size_t len, cap;
} Buf;

static void buf_add(Buf* b, const char* text) {
    size_t n = strlen(text);
    if (b->len + n + 1 > b->cap) {
        size_t cap = (b->len + n + 1) * 2;
        char* grown = (char*)realloc(b->data, cap);
        if (!grown) return;
        b->data = grown;
        b->cap = cap;
    }
    memcpy(b->data + b->len, text, n + 1);
    b->len += n;
}

/* -- files ------------------------------------------------------------------------------------ */
static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    long size;
    char* data;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = size > 0 ? (char*)malloc((size_t)size + 1) : NULL;
    if (data && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    if (data) data[size] = '\0';
    fclose(f);
    return data;
}

static void parent_dir(const char* path, char* out, size_t cap) {
    const char* slash = strrchr(path, '/');
    const char* back = strrchr(path, '\\');
    size_t n;
    if (back && (!slash || back > slash)) slash = back;
    n = slash ? (size_t)(slash - path) : 0;
    snprintf(out, cap, "%.*s", (int)n, path);
}

static const char* file_name(const char* path) {
    const char* slash = strrchr(path, '/');
    const char* back = strrchr(path, '\\');
    if (back && (!slash || back > slash)) slash = back;
    return slash ? slash + 1 : path;
}

static int exe_dir(char* out, size_t cap) {
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)cap);
    if (n == 0 || n >= cap) return 0;
#elif defined(__APPLE__)
    uint32_t n = (uint32_t)cap;
    if (_NSGetExecutablePath(out, &n) != 0) return 0;
#else
    ssize_t n = readlink("/proc/self/exe", out, cap - 1);
    if (n <= 0) return 0;
    out[n] = '\0';
#endif
    {
        char full[PATH_CAP];
        snprintf(full, sizeof full, "%s", out);
        parent_dir(full, out, cap);
    }
    return 1;
}

/* "01_box_model.html" -> "Box Model" */
static void pretty_name(const char* filename, char* out, size_t cap) {
    char s[256];
    char* dot;
    const char* p = s;
    size_t n = 0;
    int cap_next = 1;
    snprintf(s, sizeof s, "%s", filename);
    dot = strrchr(s, '.');
    if (dot) *dot = '\0';
    if (strlen(s) > 3 && s[2] == '_' && isdigit((unsigned char)s[0]) && isdigit((unsigned char)s[1]))
        p = s + 3;
    for (; *p && n + 1 < cap; ++p) {
        if (*p == '_') {
            out[n++] = ' ';
            cap_next = 1;
        } else {
            out[n++] = cap_next ? (char)toupper((unsigned char)*p) : *p;
            cap_next = 0;
        }
    }
    out[n] = '\0';
}

static int compare_samples(const void* a, const void* b) {
    return strcmp(((const Sample*)a)->filename, ((const Sample*)b)->filename);
}

static void add_sample(const char* dir, const char* name) {
    size_t n = strlen(name);
    Sample* grown;
    if (n < 5 || strcmp(name + n - 5, ".html") != 0) return;
    grown = (Sample*)realloc(g_samples, sizeof(Sample) * (size_t)(g_count + 1));
    if (!grown) return;
    g_samples = grown;
    snprintf(g_samples[g_count].filename, sizeof g_samples[g_count].filename, "%s", name);
    pretty_name(name, g_samples[g_count].name, sizeof g_samples[g_count].name);
    snprintf(g_samples[g_count].path, sizeof g_samples[g_count].path, "%s/%s", dir, name);
    ++g_count;
}

static int scan_samples(const char* dir) {
#ifdef _WIN32
    char pattern[PATH_CAP];
    WIN32_FIND_DATAA data;
    HANDLE find;
    snprintf(pattern, sizeof pattern, "%s\\*.html", dir);
    find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) add_sample(dir, data.cFileName);
    } while (FindNextFileA(find, &data));
    FindClose(find);
#else
    DIR* d = opendir(dir);
    struct dirent* entry;
    if (!d) return 0;
    while ((entry = readdir(d)) != NULL) add_sample(dir, entry->d_name);
    closedir(d);
#endif
    if (g_count > 1) qsort(g_samples, (size_t)g_count, sizeof(Sample), compare_samples);
    return g_count;
}

/* -- variant presets ------------------------------------------------------------------------ */
static int env_unset(const char* key) {
    const char* value = getenv(key);
    return !value || !*value;
}

/* applyR17EnvPreset: the user's value wins, so only unset variables are written. */
static void set_if_unset(const char* key, const char* value) {
    if (!env_unset(key)) return;
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 0);
#endif
}

static void apply_variant_preset(void) {
    if (g_variant & 1) set_if_unset("KUI_ENABLE_RETAINED_SURFACE", "1");
    if (g_variant & 2) set_if_unset("KUI_ENABLE_LAYER_PROMOTION", "1");
}

/* -- navigation bar --------------------------------------------------------------------------- */
static void build_nav(Buf* nav) {
    int i;
    char line[600];
    buf_add(nav,
        "\n<div id=\"__nav\" style=\"\n"
        "    display: flex; align-items: center; gap: 12px;\n"
        "    padding: 8px 16px; margin-bottom: 16px;\n"
        "    background: #1e293b; border: 1px solid #334155;\n"
        "    border-radius: 8px; font-family: 'Segoe UI', sans-serif;\">\n"
        "  <span style=\"font-size: 13px; font-weight: 700; color: #38bdf8;\">KUI</span>\n"
        "  <span style=\"font-size: 12px; color: #64748b;\">HTML Loader</span>\n"
        "  <select id=\"__sample_select\" style=\"\n"
        "      flex: 1; padding: 4px 8px; font-size: 13px;\n"
        "      background: #0f172a; color: #e2e8f0;\n"
        "      border: 1px solid #475569; border-radius: 4px;\">\n");
    for (i = 0; i < g_count; ++i) {
        snprintf(line, sizeof line, "    <option value=\"%d\"%s>%s</option>\n", i,
                 i == g_current ? " selected" : "", g_samples[i].filename);
        buf_add(nav, line);
    }
    buf_add(nav,
        "  </select>\n"
        "  <button id=\"__btn_open\" style=\"padding:4px 12px;font-size:12px;cursor:pointer;background:#0f172a;color:#38bdf8;border:1px solid #38bdf8;border-radius:4px;flex-shrink:0;\">Open File...</button>\n"
        "  <button id=\"__btn_inspect\" style=\"padding:4px 12px;font-size:12px;cursor:pointer;background:#0f172a;color:#94a3b8;border:1px solid #475569;border-radius:4px;flex-shrink:0;\">Inspect</button>\n"
        "  <span id=\"__filename\" style=\"font-size:11px;color:#475569;max-width:200px;overflow:hidden;\">");
    if (g_current >= 0 && g_current < g_count) buf_add(nav, g_samples[g_current].filename);
    buf_add(nav, "</span>\n</div>\n");
}

/* Inserts the bar after the <body ...> tag, or wraps the content when there is none. */
static void inject_nav(const char* content, Buf* out) {
    Buf nav = {0};
    const char* p;
    const char* body = NULL;
    build_nav(&nav);
    for (p = content; *p; ++p) {
        if (p[0] == '<' && tolower((unsigned char)p[1]) == 'b' && tolower((unsigned char)p[2]) == 'o' &&
            tolower((unsigned char)p[3]) == 'd' && tolower((unsigned char)p[4]) == 'y') {
            body = p;
            break;
        }
    }
    if (body && strchr(body, '>')) {
        const char* close = strchr(body, '>') + 1;
        char* head = (char*)malloc((size_t)(close - content) + 1);
        if (head) {
            memcpy(head, content, (size_t)(close - content));
            head[close - content] = '\0';
            buf_add(out, head);
            free(head);
        }
        buf_add(out, "\n");
        buf_add(out, nav.data ? nav.data : "");
        buf_add(out, close);
    } else if (!body) {
        buf_add(out, "<html><body>\n");
        buf_add(out, nav.data ? nav.data : "");
        buf_add(out, content);
        buf_add(out, "\n</body></html>");
    } else {
        buf_add(out, content);
    }
    free(nav.data);
}

/* -- virtual components (setupVirtualComponents) ---------------------------------------------- */
static void set_style(KuiElement* element, const char* property, const char* value) {
    kui_element_set_style_property(element, property, 0, value, 0, 0);
}

static KuiElement* vc_row(KuiDocument* document, uint32_t index, void* user_data) {
    static const char* const row_style[] = {"display", "flex", "align-items", "center", "padding",
                                            "0 12px", "font-size", "13px", NULL};
    static const char* const index_style[] = {"color", "#475569", "width", "60px", "font-size",
                                              "11px", NULL};
    KuiElement* row = kuex_make_element(document, "div", NULL, row_style);
    char text[64];
    (void)user_data;
    if (!row) return NULL;
    set_style(row, "color", index % 2 ? "#94a3b8" : "#cbd5e1");
    set_style(row, "background", index % 2 ? "transparent" : "rgba(255,255,255,0.02)");
    set_style(row, "font-family", "'Segoe UI', sans-serif");
    snprintf(text, sizeof text, "%u", (unsigned)index);
    kuex_append_release(row, kuex_make_element(document, "span", text, index_style));
    snprintf(text, sizeof text, "Item #%u%s", (unsigned)index,
             index % 1000 == 0 ? "  ** milestone **" : "");
    kuex_append_release(row, kuex_make_element(document, "span", text, NULL));
    return row;
}

static void vc_item(uint32_t index, KuiCListViewItem* item, void* user_data) {
    static const char* const icons[] = {"\xF0\x9F\x93\x84", "\xF0\x9F\x93\x81", "\xF0\x9F\x94\xA7",
                                        "\xF0\x9F\x93\xA6", "\xF0\x9F\x8C\x90", "\xE2\x9A\xA1"};
    static char text[64], subtitle[64];
    (void)user_data;
    snprintf(text, sizeof text, "List item %u", (unsigned)index);
    snprintf(subtitle, sizeof subtitle, "Description for item %u", (unsigned)index);
    item->icon = icons[index % 6];
    item->text = text;
    item->subtitle = subtitle;
    item->disabled = index % 50 == 49;
}

static void vc_list_select(uint32_t index, void* user_data) {
    (void)user_data;
    fprintf(stderr, "[ListView] Selected: %u\n", (unsigned)index);
}

static void vc_tree_select(uint32_t index, void* user_data) {
    (void)user_data;
    fprintf(stderr, "[TreeView] Selected node: %u\n", (unsigned)index);
}

static void vc_tree_toggle(uint32_t index, int expanded, void* user_data) {
    (void)user_data;
    fprintf(stderr, "[TreeView] Node %u %s\n", (unsigned)index, expanded ? "expanded" : "collapsed");
}

static uint32_t tree_child(uint32_t parent, const char* text, const char* icon) {
    uint32_t index = 0;
    kui_tree_view_add_child(g_tree, parent, text, 0, icon, 0, &index);
    return index;
}

static void vc_toggle_styles(int line) {
    kuex_set_style(g_doc, "vc-scroll-px", "background", line ? "#1e293b" : "#0ea5e9");
    kuex_set_style(g_doc, "vc-scroll-px", "color", line ? "#94a3b8" : "#ffffff");
    kuex_set_style(g_doc, "vc-scroll-px", "border-color", line ? "#334155" : "#0ea5e9");
    kuex_set_style(g_doc, "vc-scroll-line", "background", line ? "#0ea5e9" : "#1e293b");
    kuex_set_style(g_doc, "vc-scroll-line", "color", line ? "#ffffff" : "#94a3b8");
    kuex_set_style(g_doc, "vc-scroll-line", "border-color", line ? "#0ea5e9" : "#334155");
}

static void vc_scroll_unit(KuiEvent* event, void* user_data) {
    const KuiCScrollUnit unit = user_data ? KUI_C_SCROLL_LINE : KUI_C_SCROLL_PIXEL;
    (void)event;
    if (g_vlist) kui_virtual_list_set_scroll_unit(g_vlist, unit);
    if (g_list) kui_list_view_set_scroll_unit(g_list, unit);
    if (g_tree) kui_tree_view_set_scroll_unit(g_tree, unit);
    vc_toggle_styles(unit == KUI_C_SCROLL_LINE);
    fprintf(stderr, "[VirtualComponents] scrollUnit -> %s\n", unit == KUI_C_SCROLL_LINE ? "line" : "pixel");
}

static void release_components(void) {
    if (g_vlist) kui_virtual_list_release(g_vlist);
    if (g_list) kui_list_view_release(g_list);
    if (g_tree) kui_tree_view_release(g_tree);
    g_vlist = NULL;
    g_list = NULL;
    g_tree = NULL;
}

static void setup_virtual_components(void) {
    static const char* const folder = "\xF0\x9F\x93\x81";
    static const char* const file = "\xF0\x9F\x93\x84";
    KuiElement* container = kuex_by_id(g_doc, "vlist-container");
    if (container) {
        KuiCVirtualListConfig config;
        memset(&config, 0, sizeof config);
        config.total_items = 100000;
        config.item_height = 32.0f;
        config.overscan = 5;
        config.render_item = vc_row;
        kui_virtual_list_create(container, &config, &g_vlist);
        kui_element_release(container);
    }
    if ((container = kuex_by_id(g_doc, "listview-container")) != NULL) {
        KuiCListViewConfig config;
        memset(&config, 0, sizeof config);
        config.item_count = 50000;
        config.item_height = 36.0f;
        config.overscan = 5;
        config.selection_mode = KUI_C_SELECTION_SINGLE;
        config.item_provider = vc_item;
        config.on_select = vc_list_select;
        kui_list_view_create(container, &config, &g_list);
        kui_element_release(container);
    }
    if ((container = kuex_by_id(g_doc, "treeview-container")) != NULL) {
        if (kui_tree_view_create(&g_tree) == KUI_OK) {
            KuiCTreeViewConfig config;
            uint32_t src = 0, comp, hooks, pages, home, dash, settings, utils, pub = 0;
            kui_tree_view_add_node(g_tree, "src", 0, folder, 0, &src);
            comp = tree_child(src, "components", folder);
            tree_child(comp, "Button.tsx", file);
            tree_child(comp, "Input.tsx", file);
            tree_child(comp, "Modal.tsx", file);
            tree_child(comp, "Table.tsx", file);
            tree_child(comp, "Dropdown.tsx", file);
            hooks = tree_child(src, "hooks", folder);
            tree_child(hooks, "useAuth.ts", file);
            tree_child(hooks, "useQuery.ts", file);
            tree_child(hooks, "useTheme.ts", file);
            pages = tree_child(src, "pages", folder);
            home = tree_child(pages, "Home", folder);
            tree_child(home, "index.tsx", file);
            tree_child(home, "styles.css", file);
            dash = tree_child(pages, "Dashboard", folder);
            tree_child(dash, "index.tsx", file);
            tree_child(dash, "Chart.tsx", file);
            tree_child(dash, "Stats.tsx", file);
            settings = tree_child(pages, "Settings", folder);
            tree_child(settings, "Profile.tsx", file);
            tree_child(settings, "Security.tsx", file);
            utils = tree_child(src, "utils", folder);
            tree_child(utils, "api.ts", file);
            tree_child(utils, "format.ts", file);
            tree_child(utils, "validate.ts", file);
            tree_child(src, "App.tsx", file);
            tree_child(src, "index.tsx", file);
            kui_tree_view_add_node(g_tree, "public", 0, folder, 0, &pub);
            tree_child(pub, "index.html", file);
            tree_child(pub, "favicon.ico", file);
            kui_tree_view_add_node(g_tree, "package.json", 0, file, 0, NULL);
            kui_tree_view_add_node(g_tree, "tsconfig.json", 0, file, 0, NULL);
            kui_tree_view_add_node(g_tree, "README.md", 0, file, 0, NULL);
            kui_tree_view_expand(g_tree, src);
            memset(&config, 0, sizeof config);
            config.item_height = 28.0f;
            config.indent_width = 18.0f;
            config.overscan = 5;
            config.on_select = vc_tree_select;
            config.on_toggle = vc_tree_toggle;
            kui_tree_view_setup(g_tree, container, &config);
        }
        kui_element_release(container);
    }
    kuex_on(g_doc, "vc-scroll-px", "click", vc_scroll_unit, NULL);
    kuex_on(g_doc, "vc-scroll-line", "click", vc_scroll_unit, (void*)1);
    vc_toggle_styles(0); /* default visual state: Pixel mode */
}

/* -- loading ---------------------------------------------------------------------------------- */
static void wire_nav(void);

static void script_error(const char* error, size_t error_len, const char* source,
                         size_t source_len, int line, void* user_data) {
    (void)user_data;
    fprintf(stderr, "[Lua] Error in %.*s:%d \xE2\x80\x94 %.*s\n", (int)source_len, source, line,
            (int)error_len, error);
}

/* setupScriptEngine: drop the previous engine, bind a new one and run the page's scripts.
 * Loads only happen from timers or the ready callback, never inside a running script. */
static void setup_script_engine(void) {
    if (g_engine) {
        kui_script_engine_unbind_document(g_engine);
        kui_script_engine_release(g_engine);
        g_engine = NULL;
    } else if (g_runner_script) {
        kui_script_engine_unbind_document(g_runner_script);
    }
    if (kui_script_engine_create(&g_engine) != KUI_OK) {
        g_engine = NULL;
        return;
    }
    /* print() / console.log() output reaches DevTools and stderr (useful during development). */
    kui_console_set_enabled(1);
    kui_console_set_stderr_output(1);
    kui_console_set_min_level(KUI_C_LOG_DEBUG);
    kui_script_engine_set_error_handler(g_engine, script_error, NULL);
    kui_script_engine_bind_document(g_engine, g_doc);
    kui_script_engine_execute_scripts(g_engine);
    kui_window_set_script_engine(g_window, g_engine); /* re-attaches DevTools too */
}

/* Shared part of loadSample / loadExternalFile; `content` already carries the bar. */
static void load_page(const char* html, const char* source_path, const char* title_name) {
    char dir[PATH_CAP], base[PATH_CAP + 2], title[512];
    parent_dir(source_path, dir, sizeof dir);
    snprintf(base, sizeof base, "%s/", dir);
    kui_document_set_base_path(g_doc, base, 0);
    release_components();
    kui_document_load_html(g_doc, html, 0);
    snprintf(title, sizeof title, "KUI HTML Loader \xE2\x80\x94 %s", title_name);
    kui_window_set_title(g_window, title, 0);
    wire_nav();
    setup_script_engine();
}

static void load_sample(int index) {
    Buf html = {0};
    char* content;
    if (index < 0 || index >= g_count) return;
    g_current = index;
    content = read_file(g_samples[index].path);
    if (!content) {
        fprintf(stderr, "[HTML Loader] Failed to read: %s\n", g_samples[index].path);
        return;
    }
    inject_nav(content, &html);
    free(content);
    if (!html.data) return;
    load_page(html.data, g_samples[index].path, g_samples[index].name);
    fprintf(stderr, "[HTML Loader] Loaded: %s (%s)\n", g_samples[index].name,
            g_samples[index].filename);
    free(html.data);
    if (strstr(g_samples[index].filename, "virtual_components")) setup_virtual_components();
}

static void load_external_file(const char* path) {
    Buf html = {0};
    char* content = read_file(path);
    if (!content) {
        fprintf(stderr, "[HTML Loader] Failed to read: %s\n", path);
        return;
    }
    g_current = -1; /* the bar then shows no sample selected */
    inject_nav(content, &html);
    free(content);
    if (!html.data) return;
    load_page(html.data, path, file_name(path));
    fprintf(stderr, "[HTML Loader] Loaded external file: %s\n", path);
    free(html.data);
    kuex_set_text(g_doc, "__filename", file_name(path));
}

static void deferred_load(void* user_data) {
    (void)user_data;
    if (g_pending_path[0]) {
        char path[PATH_CAP];
        snprintf(path, sizeof path, "%s", g_pending_path);
        g_pending_path[0] = '\0';
        load_external_file(path);
    } else if (g_pending_index >= 0) {
        const int index = g_pending_index;
        g_pending_index = -1;
        load_sample(index);
    }
}

/* -- navigation bar handlers ------------------------------------------------------------------ */
static void on_sample_change(KuiEvent* event, void* user_data) {
    char text[32];
    int index;
    (void)event;
    (void)user_data;
    kuex_eval_text(g_engine,
                   "return tostring(document:getElementById('__sample_select').selectedIndex)",
                   text, sizeof text);
    index = text[0] ? atoi(text) : -1;
    if (index >= 0 && index < g_count && index != g_current) {
        g_pending_index = index;
        kui_platform_set_timeout(deferred_load, NULL, 0, NULL);
    }
}

static void on_open_file(KuiEvent* event, void* user_data) {
    static const char* const filters[] = {"*.html;*.htm", "*.*"};
    KuiCFileDialogOptions options;
    char path[PATH_CAP];
    size_t len = 0;
    (void)event;
    (void)user_data;
    memset(&options, 0, sizeof options);
    options.title = "Open HTML File";
    options.filters = filters;
    options.filter_count = 2;
    if (kui_platform_open_file_dialog(&options, path, sizeof path, &len) == KUI_OK && len > 0) {
        path[len < sizeof path ? len : sizeof path - 1] = '\0';
        snprintf(g_pending_path, sizeof g_pending_path, "%s", path);
        kui_platform_set_timeout(deferred_load, NULL, 0, NULL);
    }
}

static void on_inspect(KuiEvent* event, void* user_data) {
    KuiElement* button = kuex_by_id(g_doc, "__btn_inspect");
    (void)event;
    (void)user_data;
    g_inspect = !g_inspect;
    if (!button) return;
    kui_element_set_attribute(button, "style", 0,
        g_inspect ? "padding:4px 12px;font-size:12px;cursor:pointer;"
                    "background:#0c4a6e;color:#38bdf8;"
                    "border:1px solid #38bdf8;border-radius:4px;flex-shrink:0;"
                  : "padding:4px 12px;font-size:12px;cursor:pointer;"
                    "background:#0f172a;color:#94a3b8;"
                    "border:1px solid #475569;border-radius:4px;flex-shrink:0;",
        0);
    kui_element_release(button);
}

static void wire_nav(void) {
    kuex_on(g_doc, "__sample_select", "change", on_sample_change, NULL);
    kuex_on(g_doc, "__btn_open", "click", on_open_file, NULL);
    kuex_on(g_doc, "__btn_inspect", "click", on_inspect, NULL);
}

static void on_window_close(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    release_components();
    if (g_engine) {
        kui_window_set_script_engine(g_window, NULL);
        kui_script_engine_unbind_document(g_engine);
        kui_script_engine_release(g_engine);
        g_engine = NULL;
    }
}

/* <exe>/samples, ./samples, examples/html_loader/samples, then the package layout. */
static void find_samples(void) {
    static const char* const dirs[] = {"samples", "examples/html_loader/samples"};
    char base[PATH_CAP], dir[PATH_CAP];
    size_t i;
    if (exe_dir(base, sizeof base)) {
        snprintf(dir, sizeof dir, "%s/samples", base);
        scan_samples(dir);
    }
    for (i = 0; g_count == 0 && i < sizeof dirs / sizeof dirs[0]; ++i) scan_samples(dirs[i]);
    if (g_count == 0 && exe_dir(base, sizeof base)) {
        snprintf(dir, sizeof dir, "%s/assets/native_samples", base);
        scan_samples(dir);
    }
}

static void print_usage(void) {
    printf(
        "Usage: kui_html_loader [options] [sample]\n"
        "  --file <path.html>   load an arbitrary HTML file\n"
        "  --list               print available samples and exit\n"
        "  --dev                enable dev console (errors/warnings to stderr)\n"
        "  --devtools [port]    start DevTools server (default 9229)\n"
        "  --devtools-port N    explicit DevTools server port\n"
        "  --no-token           disable DevTools WebSocket token auth (testing)\n"
        "Window shape (defaults: 1024x768 decorated window):\n"
        "  --size WxH           initial client size, e.g. 1280x720\n"
        "  --frameless          frameless window (no title bar / borders)\n"
        "  --shadow             window shadow (frameless only)\n"
        "  --radius <px>        rounded frameless window, radius in DIP\n"
        "  --sizeable           edge-resizable frameless window\n"
        "  --show-after-load    create hidden, load the page, render one frame, then show()\n"
        "                       (the order real apps such as mini-erp-burn use)\n"
        "  --dpr <x>            simulated backend device pixel ratio (measurement only)\n");
}

/* Case-insensitive substring match on the file name; the first match wins. */
static void select_sample(const char* name) {
    char needle[256], hay[256];
    int i;
    size_t k;
    snprintf(needle, sizeof needle, "%s", name);
    for (k = 0; needle[k]; ++k) needle[k] = (char)tolower((unsigned char)needle[k]);
    for (i = 0; i < g_count; ++i) {
        snprintf(hay, sizeof hay, "%s", g_samples[i].filename);
        for (k = 0; hay[k]; ++k) hay[k] = (char)tolower((unsigned char)hay[k]);
        if (strstr(hay, needle)) {
            g_current = i;
            return;
        }
    }
    fprintf(stderr, "[HTML Loader] No sample matching '%s'. Use --list to see available.\n", name);
}

/* The stack trace writer and the crash reporter, as the original installs them before Platform::init. */
static void install_crash_diagnostics(void) {
    KuiCStackTraceConfig stack;
    KuiCCrashReporterConfig crash;
    char version[32];
    int installed = 0;

    memset(&stack, 0, sizeof stack);
    stack.struct_size = sizeof stack;
    stack.output_mode = KUI_C_STACK_TRACE_FILE | KUI_C_STACK_TRACE_STDERR | KUI_C_STACK_TRACE_MESSAGE_BOX;
    stack.app_name = "kui_html_loader";
    stack.write_mini_dump = 1;
    stack.max_frames = 96;
    if (kui_stack_trace_install(&stack, &installed) != KUI_OK || !installed)
        fprintf(stderr, "[StackTrace] Warning: in-process crash handler install failed.\n");
    snprintf(version, sizeof version, "%d.%d.%d", KUI_C_API_VERSION_MAJOR, KUI_C_API_VERSION_MINOR,
             KUI_C_API_VERSION_PATCH);
    memset(&crash, 0, sizeof crash);
    crash.struct_size = sizeof crash;
    crash.app_name = "kui_html_loader";
    crash.app_version = version;
    installed = 0;
    if (kui_crash_reporter_install(&crash, &installed) != KUI_OK || !installed)
        fprintf(stderr, "[CrashReporter] Warning: crash reporting unavailable.\n");
}

int kui_c_example_prepare(int argc, char** argv, KuiWindowProps* props, int* exit_code) {
    const char* sample_to_find = NULL;
    int i;
    apply_variant_preset();
    find_samples();
    *exit_code = 0;
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_usage();
        return 1;
    }
    if (argc > 1 && strcmp(argv[1], "--list") == 0) {
        printf("Available samples (%d):\n", g_count);
        for (i = 0; i < g_count; ++i)
            printf("  [%d] %-30s %s\n", i, g_samples[i].name, g_samples[i].filename);
        return 1;
    }
    for (i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (strcmp(arg, "--size") == 0 && i + 1 < argc) {
            unsigned w = 0, h = 0;
            if (sscanf(argv[++i], "%ux%u", &w, &h) == 2 && w >= 160 && h >= 120 && w <= 16384 &&
                h <= 16384) {
                props->width = (int)w;
                props->height = (int)h;
            } else {
                fprintf(stderr, "[HTML Loader] Ignoring invalid --size '%s' (expected WxH)\n", argv[i]);
            }
        } else if (strcmp(arg, "--frameless") == 0) {
            props->frameless = 1;
            props->no_decorations = 1;
        } else if (strcmp(arg, "--shadow") == 0) {
            props->frameless_shadow = 1;
        } else if (strcmp(arg, "--sizeable") == 0) {
            g_sizeable = 1;
        } else if (strcmp(arg, "--show-after-load") == 0) {
            g_show_after_load = 1;
        } else if (strcmp(arg, "--radius") == 0 && i + 1 < argc) {
            const double r = atof(argv[++i]);
            if (r >= 0.0 && r <= 1024.0) {
                g_has_radius = 1;
                g_radius = (float)r;
            }
        } else if (strcmp(arg, "--dpr") == 0 && i + 1 < argc) {
            g_backend_dpr = (float)atof(argv[++i]);
        } else if (strcmp(arg, "--dev") == 0) {
            g_dev_mode = 1;
        } else if (strcmp(arg, "--devtools") == 0) {
            g_devtools = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                const int p = atoi(argv[i + 1]);
                if (p > 0 && p < 65536) {
                    g_devtools_port = p;
                    ++i;
                }
            }
        } else if (strcmp(arg, "--devtools-port") == 0 && i + 1 < argc) {
            const int p = atoi(argv[++i]);
            if (p > 0 && p < 65536) {
                g_devtools_port = p;
                g_devtools = 1;
            }
        } else if (strcmp(arg, "--no-token") == 0) {
            g_no_token = 1;
        } else if (strcmp(arg, "--file") == 0 && i + 1 < argc) {
            g_file_to_load = argv[++i];
        } else if (arg[0] != '-') {
            sample_to_find = arg;
        }
    }
    if (g_dev_mode) {
        kui_console_set_enabled(1);
        kui_console_set_stderr_output(1);
        kui_console_set_min_level(KUI_C_LOG_DEBUG);
        fprintf(stderr, "[DEV] Developer console enabled\n");
    }
    install_crash_diagnostics();
    if (sample_to_find) select_sample(sample_to_find);
    return 0;
}

int kui_c_example_before_load(const KuiCExampleHost* host) {
    if (g_sizeable) kui_window_set_frameless_sizeable(host->window, 1);
    if (g_has_radius) kui_window_set_frame_radius(host->window, g_radius);
    if (g_show_after_load) kui_window_hide(host->window);
    return 0;
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_window = host->window;
    g_doc = host->document;
    g_runner_script = host->script;
    if (g_backend_dpr > 0.0f) kui_document_set_render_device_pixel_ratio(g_doc, g_backend_dpr);
    kui_platform_set_f12_devtools_enabled(1);
    kui_window_add_event_listener(g_window, "close", 0, on_window_close, NULL, 0, NULL);
    if (g_devtools) {
        int running = 0, started = 0;
        kui_window_get_devtools_state(g_window, &running, NULL, NULL);
        if (!running) kui_window_start_devtools(g_window, (uint16_t)g_devtools_port, &started);
        if (running || started) {
            char url[512];
            if (g_no_token) kui_window_set_devtools_token_auth(g_window, 0);
            size_t len = 0;
            if (kui_window_get_devtools_url(g_window, url, sizeof url, &len) == KUI_OK)
                fprintf(stderr, "[DevTools] Open %s in your browser to inspect\n", url);
        }
    }
    /* No samples and no --file: the runner's page is the original's "No samples found" page. */
    if (g_file_to_load) load_external_file(g_file_to_load);
    else if (g_count > 0) load_sample(g_current);
    if (g_show_after_load) {
        kui_window_process_frame(g_window);
        kui_window_show(g_window);
    }
    return 0;
}
