/* Host logic of examples/script_example/main.cpp: a browser for HTML files with embedded Lua.
 * It scans script_html/ next to the executable (the original's POST_BUILD copy of
 * tests/script/html; also --dir / --file / a positional path / --list), wraps each file's body
 * and first <style> in a page with a nav bar (prev / counter / next / title / file name / reload)
 * and a status bar, runs the file's <script> blocks in a fresh script engine, and shows the
 * #result / #detail text in the status bar.
 *
 * Differences from the original:
 * - The original adds its Left / Right / F5 keydown listener to doc->body() before any page is
 *   loaded, when the document has no body, so keyboard navigation never works. This port adds
 *   the listener to the body of every page it loads (what the original means to do).
 * - --list prints the list and quits right away, but the window already exists by then (the
 *   example host code runs after the runner opened the window).
 * Like the original it ticks the script timers at 0 and 100 ms right after the scripts
 * (kui_script_engine_tick_timers), so short timers land before #result is read. */
#include "kui_c_example_dom.h"

#include <ctype.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

typedef struct HtmlEntry {
    char name[256];     /* display name */
    char filename[256]; /* e.g. "01_basic_eval.html" */
    char path[1400];    /* full path */
} HtmlEntry;

static KuiDocument* g_doc;
static KuiWindow* g_window;
static HtmlEntry* g_entries;
static int g_entry_count;
static int g_current_index;
static KuiScriptEngine* g_engine; /* fresh engine per loaded entry */

/* ---- Utility ---------------------------------------------------------------------------- */

typedef struct Text {
    char* data;
    size_t length;
    size_t capacity;
} Text;

static void text_append_n(Text* text, const char* value, size_t length) {
    if (text->length + length + 1 > text->capacity) {
        size_t capacity = text->capacity ? text->capacity : 4096;
        char* data;
        while (text->length + length + 1 > capacity) capacity *= 2;
        data = (char*)realloc(text->data, capacity);
        if (!data) return;
        text->data = data;
        text->capacity = capacity;
    }
    memcpy(text->data + text->length, value, length);
    text->length += length;
    text->data[text->length] = '\0';
}

static void text_append(Text* text, const char* value) {
    text_append_n(text, value, strlen(value));
}

static char* read_file(const char* path, size_t* out_size) {
    FILE* stream = fopen(path, "rb");
    char* data = NULL;
    long size;
    if (!stream) return NULL;
    fseek(stream, 0, SEEK_END);
    size = ftell(stream);
    fseek(stream, 0, SEEK_SET);
    if (size >= 0) data = (char*)malloc((size_t)size + 1);
    if (data && fread(data, 1, (size_t)size, stream) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(stream);
    if (!data) return NULL;
    data[size] = '\0';
    *out_size = (size_t)size;
    return data;
}

static void pretty_name(const char* filename, char* out, size_t capacity) {
    char s[256];
    char* dot;
    const char* p;
    size_t n = 0;
    int cap_next = 1;
    snprintf(s, sizeof s, "%s", filename);
    dot = strrchr(s, '.');
    if (dot) *dot = '\0';
    p = s;
    /* Remove leading number prefix like "01_" */
    if (strlen(s) > 3 && s[2] == '_' && isdigit((unsigned char)s[0]) &&
        isdigit((unsigned char)s[1])) {
        p = s + 3;
    }
    for (; *p && n + 1 < capacity; ++p) {
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

static const char* base_name(const char* path) {
    const char* slash = strrchr(path, '/');
    const char* back = strrchr(path, '\\');
    if (back && (!slash || back > slash)) slash = back;
    return slash ? slash + 1 : path;
}

static int is_directory(const char* path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

static int is_regular_file(const char* path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
#endif
}

static int exe_dir(char* out, size_t capacity) {
    char* slash;
#ifdef _WIN32
    DWORD length = GetModuleFileNameA(NULL, out, (DWORD)capacity);
    if (length == 0 || length >= capacity) return 0;
    out[length] = '\0';
    for (slash = out; *slash; ++slash) {
        if (*slash == '\\') *slash = '/';
    }
#elif defined(__APPLE__)
    uint32_t length = (uint32_t)capacity;
    if (_NSGetExecutablePath(out, &length) != 0) return 0;
#else
    ssize_t length = readlink("/proc/self/exe", out, capacity - 1);
    if (length <= 0) return 0;
    out[length] = '\0';
#endif
    slash = strrchr(out, '/');
    if (!slash) return 0;
    *slash = '\0';
    return 1;
}

/* ---- Scan directory for .html files ----------------------------------------------------- */

static int compare_names(const void* a, const void* b) {
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

static int has_html_extension(const char* name) {
    const char* dot = strrchr(name, '.');
    return dot && strcmp(dot, ".html") == 0;
}

static void scan_directory(const char* dir) {
    char** files = NULL;
    int count = 0;
    int capacity = 0;
    int i;
    free(g_entries);
    g_entries = NULL;
    g_entry_count = 0;
    if (!is_directory(dir)) return;
    {
#ifdef _WIN32
        char pattern[1200];
        WIN32_FIND_DATAA data;
        HANDLE find;
        snprintf(pattern, sizeof pattern, "%s/*", dir);
        find = FindFirstFileA(pattern, &data);
        if (find == INVALID_HANDLE_VALUE) return;
        do {
            const char* name = data.cFileName;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
#else
        DIR* handle = opendir(dir);
        struct dirent* item;
        if (!handle) return;
        while ((item = readdir(handle)) != NULL) {
            const char* name = item->d_name;
            char full[1400];
            snprintf(full, sizeof full, "%s/%s", dir, name);
            if (!is_regular_file(full)) continue;
#endif
            if (!has_html_extension(name)) continue;
            if (count == capacity) {
                char** grown;
                capacity = capacity ? capacity * 2 : 64;
                grown = (char**)realloc(files, (size_t)capacity * sizeof *files);
                if (!grown) break;
                files = grown;
            }
            files[count] = (char*)malloc(strlen(name) + 1);
            if (files[count]) memcpy(files[count++], name, strlen(name) + 1);
#ifdef _WIN32
        } while (FindNextFileA(find, &data));
        FindClose(find);
#else
        }
        closedir(handle);
#endif
    }
    qsort(files, (size_t)count, sizeof *files, compare_names);
    g_entries = (HtmlEntry*)calloc(count ? (size_t)count : 1, sizeof *g_entries);
    for (i = 0; i < count; ++i) {
        if (g_entries) {
            HtmlEntry* e = &g_entries[g_entry_count++];
            snprintf(e->filename, sizeof e->filename, "%s", files[i]);
            pretty_name(files[i], e->name, sizeof e->name);
            snprintf(e->path, sizeof e->path, "%s/%s", dir, files[i]);
        }
        free(files[i]);
    }
    free(files);
}

/* ---- Navigation UI and status bar HTML -------------------------------------------------- */

static void append_nav_html(Text* page) {
    char counter[48];
    const HtmlEntry* e = g_current_index >= 0 && g_current_index < g_entry_count
                             ? &g_entries[g_current_index] : NULL;
    text_append(page,
        "\n<div id=\"__nav\" style=\"\n"
        "    display: flex; align-items: center; gap: 8px;\n"
        "    padding: 6px 12px;\n"
        "    background: #1e293b; border-bottom: 1px solid #334155;\n"
        "    font-family: 'Segoe UI', sans-serif; position: relative;\">\n"
        "  <span style=\"font-size: 13px; font-weight: 700; color: #38bdf8;\">KUI</span>\n"
        "  <span style=\"font-size: 11px; color: #64748b;\">Script</span>\n"
        "  <div id=\"__btn_prev\" style=\"\n"
        "      padding: 2px 10px; font-size: 14px; color: #94a3b8;\n"
        "      background: #0f172a; border: 1px solid #475569; border-radius: 4px;\n"
        "      cursor: pointer;\">&#9664;</div>\n"
        "  <span id=\"__counter\" style=\"font-size: 11px; color: #94a3b8; min-width: 50px; "
        "text-align: center;\">");
    snprintf(counter, sizeof counter, "%d / %d", g_current_index + 1, g_entry_count);
    text_append(page, counter);
    text_append(page,
        "</span>\n"
        "  <div id=\"__btn_next\" style=\"\n"
        "      padding: 2px 10px; font-size: 14px; color: #94a3b8;\n"
        "      background: #0f172a; border: 1px solid #475569; border-radius: 4px;\n"
        "      cursor: pointer;\">&#9654;</div>\n"
        "  <span id=\"__title\" style=\"\n"
        "      flex: 1; font-size: 13px; font-weight: 600; color: #e2e8f0;\n"
        "      overflow: hidden; text-overflow: ellipsis; white-space: nowrap;\">");
    if (e) text_append(page, e->name);
    text_append(page,
        "</span>\n"
        "  <span id=\"__filename\" style=\"font-size: 10px; color: #475569;\">");
    if (e) text_append(page, e->filename);
    text_append(page,
        "</span>\n"
        "  <div id=\"__btn_reload\" style=\"\n"
        "      padding: 2px 10px; font-size: 13px; color: #94a3b8;\n"
        "      background: #0f172a; border: 1px solid #475569; border-radius: 4px;\n"
        "      cursor: pointer;\">&#8635;</div>\n"
        "</div>\n");
}

static const char k_status_html[] =
    "\n<div id=\"__status\" style=\"\n"
    "    display: flex; align-items: center; gap: 8px;\n"
    "    padding: 4px 12px;\n"
    "    background: #0f172a; border-top: 1px solid #334155;\n"
    "    font-family: 'Segoe UI', sans-serif; font-size: 11px; color: #64748b;\">\n"
    "  <span>Result:</span>\n"
    "  <span id=\"__result_display\" style=\"font-weight: 700; color: #94a3b8;\">"
    "\xE2\x80\x94</span>\n"
    "  <span id=\"__detail_display\" style=\"flex: 1; color: #475569; overflow: hidden; "
    "text-overflow: ellipsis; white-space: nowrap;\"></span>\n"
    "</div>\n";

static const char k_page_head[] =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "<style>\n"
    "* { margin: 0; padding: 0; box-sizing: border-box; }\n"
    "html, body { height: 100vh; display: flex; flex-direction: column; background: #0f172a; }\n"
    "#__content_area {\n"
    "    flex: 1; overflow: auto; padding: 16px;\n"
    "    background: #0f172a; color: #e2e8f0;\n"
    "    font-family: 'Segoe UI', sans-serif;\n"
    "}\n"
    "</style>\n"
    "</head>\n"
    "<body>\n";

/* ---- Load and execute an HTML file with scripting --------------------------------------- */

static void load_entry(int index);

/* Copies #result / #detail into the status bar (the original's post-load block). */
static void update_status(void) {
    KuiElement* result_el = kuex_by_id(g_doc, "result");
    KuiElement* detail_el = kuex_by_id(g_doc, "detail");
    KuiElement* result_display = kuex_by_id(g_doc, "__result_display");
    KuiElement* detail_display = kuex_by_id(g_doc, "__detail_display");
    char text[1024];
    if (result_display) {
        if (result_el) {
            kuex_element_text(result_el, text, sizeof text);
            kui_element_set_text_content(result_display, text, 0);
            kui_element_set_style_property(result_display, "color", 0,
                                           strcmp(text, "PASS") == 0 ? "#22c55e" : "#ef4444",
                                           0, 0);
        } else {
            kui_element_set_text_content(result_display, "(no #result element)", 0);
            kui_element_set_style_property(result_display, "color", 0, "#f59e0b", 0, 0);
        }
    }
    if (detail_display && detail_el) {
        kuex_element_text(detail_el, text, sizeof text);
        kui_element_set_text_content(detail_display, text, 0);
    }
    if (result_el) kui_element_release(result_el);
    if (detail_el) kui_element_release(detail_el);
    if (result_display) kui_element_release(result_display);
    if (detail_display) kui_element_release(detail_display);
}

static void on_prev(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    if (g_current_index > 0) load_entry(g_current_index - 1);
}

static void on_next(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    if (g_current_index < g_entry_count - 1) load_entry(g_current_index + 1);
}

static void on_reload(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    load_entry(g_current_index);
}

/* Keyboard navigation: Left / Right arrows to switch, F5 to reload. */
static void on_keydown(KuiEvent* event, void* user_data) {
    char key[32];
    size_t length = 0;
    (void)user_data;
    if (kui_event_key_get_key(event, key, sizeof key, &length) != KUI_OK) return;
    key[sizeof key - 1] = '\0';
    if (strcmp(key, "ArrowLeft") == 0 || strcmp(key, "Left") == 0) {
        if (g_current_index > 0) load_entry(g_current_index - 1);
    } else if (strcmp(key, "ArrowRight") == 0 || strcmp(key, "Right") == 0) {
        if (g_current_index < g_entry_count - 1) load_entry(g_current_index + 1);
    } else if (strcmp(key, "F5") == 0) {
        load_entry(g_current_index);
    }
}

static void add_keyboard_listener(void) {
    KuiElement* body = NULL;
    if (kui_document_body(g_doc, &body) != KUI_OK || !body) return;
    kui_element_add_event_listener(body, "keydown", 0, on_keydown, NULL, NULL);
    kui_element_release(body);
}

static void wire_nav_events(void) {
    KuiElement* element;
    if ((element = kuex_by_id(g_doc, "__btn_prev")) != NULL) {
        kui_element_add_event_listener(element, "click", 0, on_prev, NULL, NULL);
        kui_element_release(element);
    }
    if ((element = kuex_by_id(g_doc, "__btn_next")) != NULL) {
        kui_element_add_event_listener(element, "click", 0, on_next, NULL, NULL);
        kui_element_release(element);
    }
    if ((element = kuex_by_id(g_doc, "__btn_reload")) != NULL) {
        kui_element_add_event_listener(element, "click", 0, on_reload, NULL, NULL);
        kui_element_release(element);
    }
    add_keyboard_listener();
}

static char* lower_copy(const char* value, size_t length) {
    char* lower = (char*)malloc(length + 1);
    size_t i;
    if (!lower) return NULL;
    for (i = 0; i < length; ++i) lower[i] = (char)tolower((unsigned char)value[i]);
    lower[length] = '\0';
    return lower;
}

static void load_entry(int index) {
    const HtmlEntry* entry;
    Text page = {NULL, 0, 0};
    char* content;
    char* lower;
    const char* body_open;
    const char* body_close;
    const char* style_start;
    const char* style_end;
    char title[400];
    size_t size = 0;
    if (index < 0 || index >= g_entry_count) return;
    g_current_index = index;
    entry = &g_entries[index];

    content = read_file(entry->path, &size);
    if (!content || size == 0) {
        fprintf(stderr, "[Script Example] Failed to read: %s\n", entry->path);
        free(content);
        return;
    }
    lower = lower_copy(content, size);
    if (!lower) {
        free(content);
        return;
    }

    /* Build a wrapper page: nav + content area + status bar */
    text_append(&page, k_page_head);
    append_nav_html(&page);
    text_append(&page, "<div id=\"__content_area\">\n");

    /* Also extract the first <style> block */
    style_start = strstr(lower, "<style>");
    style_end = strstr(lower, "</style>");
    if (style_start && style_end) {
        text_append_n(&page, content + (style_start - lower),
                      (size_t)(style_end + 8 - style_start));
        text_append(&page, "\n");
    }
    /* Inner body content (or the whole file when it has no body tags) */
    body_open = strstr(lower, "<body");
    body_close = strstr(lower, "</body>");
    if (body_open && body_close) {
        const char* close_tag = strchr(body_open, '>');
        if (close_tag && close_tag < body_close) {
            text_append_n(&page, content + (close_tag + 1 - lower),
                          (size_t)(body_close - close_tag - 1));
        }
    } else {
        text_append_n(&page, content, size);
    }
    text_append(&page, "\n</div>\n");
    text_append(&page, k_status_html);
    text_append(&page, "\n</body>\n</html>");

    if (page.data) kui_document_load_html(g_doc, page.data, page.length);
    free(page.data);

    /* Update window title */
    snprintf(title, sizeof title, "KUI Script Example \xE2\x80\x94 %s", entry->name);
    kui_window_set_title(g_window, title, 0);

    /* Create / reset the script engine and run the <script> blocks of the original file */
    {
        KuiScriptEngine* engine = NULL;
        if (kui_script_engine_create(&engine) == KUI_OK) {
            const char* search = lower;
            int script_index = 0;
            if (g_engine) kui_script_engine_release(g_engine);
            g_engine = engine;
            kui_script_engine_bind_document(g_engine, g_doc);
            for (;;) {
                const char* open = strstr(search, "<script>");
                const char* close;
                char source[300];
                if (!open) break;
                close = strstr(open, "</script>");
                if (!close) break;
                snprintf(source, sizeof source, "%s#%d", entry->filename, script_index++);
                kui_script_engine_eval(g_engine, content + (open + 8 - lower),
                                       (size_t)(close - open - 8), source, 0, NULL, NULL, 0, NULL);
                search = close + 9;
            }
            /* Tick timers: the original ticks 0 and 100 ms right after the scripts. */
            kui_script_engine_tick_timers(g_engine, 0.0);
            kui_script_engine_tick_timers(g_engine, 100.0);
        }
    }
    free(lower);
    free(content);

    /* Update status bar with the script result */
    update_status();

    wire_nav_events();
    fprintf(stderr, "[Script Example] Loaded: %s\n", entry->filename);
}

/* ---- Arguments and start-up ------------------------------------------------------------- */

int kui_c_example_ready(const KuiCExampleHost* host) {
    static const char* const default_dirs[] = {
        "script_html",                    /* next to exe */
        "assets/script_html",             /* in the SDK packages: assets/ next to the exe */
        "tests/script/html",              /* from project root */
        "../tests/script/html",
        "../../tests/script/html",
        "../../../tests/script/html",
        "../../../../tests/script/html",
    };
    char dir_to_scan[1200] = "";
    char file_to_load[1200] = "";
    int list_mode = 0;
    int argc;
    char** argv;
    int i;
    size_t d;
    g_doc = host->document;
    g_window = host->window;
    kui_document_register_builtin_components(g_doc);

    argc = host->argc;
    argv = host->argv;
    for (i = 1; argv && i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0) {
            list_mode = 1;
        } else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            snprintf(file_to_load, sizeof file_to_load, "%s", argv[++i]);
        } else if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) {
            snprintf(dir_to_scan, sizeof dir_to_scan, "%s", argv[++i]);
        } else if (argv[i][0] != '-') {
            /* Positional: either a file or a directory */
            if (is_regular_file(argv[i])) {
                snprintf(file_to_load, sizeof file_to_load, "%s", argv[i]);
            } else {
                snprintf(dir_to_scan, sizeof dir_to_scan, "%s", argv[i]);
            }
        }
    }

    /* Default: scan script_html / tests/script/html relative to the exe, then the cwd */
    if (!dir_to_scan[0] && !file_to_load[0]) {
        char exe[1024];
        if (exe_dir(exe, sizeof exe)) {
            for (d = 0; d < sizeof default_dirs / sizeof default_dirs[0]; ++d) {
                char candidate[1200];
                snprintf(candidate, sizeof candidate, "%s/%s", exe, default_dirs[d]);
                if (is_directory(candidate)) {
                    snprintf(dir_to_scan, sizeof dir_to_scan, "%s", candidate);
                    break;
                }
            }
        }
        for (d = 0; !dir_to_scan[0] && d < sizeof default_dirs / sizeof default_dirs[0]; ++d) {
            if (is_directory(default_dirs[d])) {
                snprintf(dir_to_scan, sizeof dir_to_scan, "%s", default_dirs[d]);
            }
        }
    }
    if (dir_to_scan[0]) scan_directory(dir_to_scan);

    if (list_mode) {
        printf("Available HTML files (%d):\n", g_entry_count);
        for (i = 0; i < g_entry_count; i++) {
            printf("  [%2d] %-30s %s\n", i, g_entries[i].name, g_entries[i].filename);
        }
        fflush(stdout);
        kui_platform_quit(0);
        return 0;
    }

    if (file_to_load[0]) {
        /* loadSingleFile: a one-entry list */
        free(g_entries);
        g_entries = (HtmlEntry*)calloc(1, sizeof *g_entries);
        if (!g_entries) return 1;
        g_entry_count = 1;
        snprintf(g_entries[0].path, sizeof g_entries[0].path, "%s", file_to_load);
        snprintf(g_entries[0].filename, sizeof g_entries[0].filename, "%s",
                 base_name(file_to_load));
        pretty_name(g_entries[0].filename, g_entries[0].name, sizeof g_entries[0].name);
        g_current_index = 0;
        load_entry(0);
    } else if (g_entry_count > 0) {
        load_entry(0);
    } else {
        /* The packaged page is the original's "No HTML files found" page; keep it. */
        add_keyboard_listener();
    }
    return 0;
}
