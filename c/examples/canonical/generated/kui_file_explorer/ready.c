/* Copied by scripts/generate_c_examples.py from examples/c/ports/kui_file_explorer.c; edit that file, not this copy. */
/* Host logic of examples/file_explorer/main.cpp: lists a real directory into #file-grid (folders
 * first, then by name; icon text and class by extension), builds the #breadcrumb from the path
 * and a "Quick Access" #sidebar. Clicking a folder, a breadcrumb segment or a quick link
 * navigates there; clicking a file does nothing.
 *
 * The original starts in (and every quick link points at) the current directory; the
 * environment variable KUI_EXAMPLE_START_DIR overrides that start directory (test hook).
 * Instead of one listener per created element, each of the three containers listens once and
 * the clicked item carries its target directory in a data-path attribute. */
#include "kui_c_example_dom.h"

#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define SEP '\\'
#else
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#define SEP '/'
#endif

#define PATH_CAP 4096

typedef struct Entry {
    char* name;
    int is_dir;
} Entry;

static KuiDocument* g_doc;
static char g_start[PATH_CAP];

static int is_sep(char c) {
#ifdef _WIN32
    return c == '\\' || c == '/';
#else
    return c == '/';
#endif
}

/* Length of the root part ("C:\", "\\server\share\" or "/"); 0 when there is none. */
static size_t root_length(const char* path) {
#ifdef _WIN32
    if (path[0] && path[1] == ':') return is_sep(path[2]) ? 3 : 2;
    if (is_sep(path[0]) && is_sep(path[1])) {
        const char* p = path + 2;
        int parts = 0;
        while (*p && parts < 2) {
            if (is_sep(*p)) ++parts;
            ++p;
        }
        return (size_t)(p - path);
    }
#endif
    return is_sep(path[0]) ? 1 : 0;
}

#ifdef _WIN32
static void to_utf8(const wchar_t* wide, char* out, size_t capacity) {
    if (!WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int)capacity, NULL, NULL)) out[0] = '\0';
}
#endif

/* Absolute path with native separators and no trailing separator (unless it is the root). */
static void normalise(const char* in, char* out) {
#ifdef _WIN32
    wchar_t wide[PATH_CAP], full[PATH_CAP];
    out[0] = '\0';
    if (!MultiByteToWideChar(CP_UTF8, 0, in, -1, wide, PATH_CAP) ||
        !GetFullPathNameW(wide, PATH_CAP, full, NULL)) {
        snprintf(out, PATH_CAP, "%s", in);
    } else {
        to_utf8(full, out, PATH_CAP);
    }
#else
    if (!realpath(in, out)) snprintf(out, PATH_CAP, "%s", in);
#endif
    {
        size_t length = strlen(out), root = root_length(out);
        char* c;
        for (c = out; *c; ++c) if (is_sep(*c)) *c = SEP;
        while (length > root && is_sep(out[length - 1])) out[--length] = '\0';
    }
}

static void lower_extension(const char* name, char* ext, size_t capacity) {
    const char* dot = strrchr(name, '.');
    size_t i = 0;
    if (!dot || dot == name) dot = ""; /* fs::path: ".hidden" has no extension */
    for (; dot[i] && i + 1 < capacity; ++i) {
        ext[i] = (char)((dot[i] >= 'A' && dot[i] <= 'Z') ? dot[i] - 'A' + 'a' : dot[i]);
    }
    ext[i] = '\0';
}

static int ext_in(const char* ext, const char* const* list) {
    for (; *list; ++list) if (!strcmp(ext, *list)) return 1;
    return 0;
}

/* The original's getFileIcon / getIconClass (their extension lists differ: .svg and .rs only get
 * an icon text, not a class). */
static void icon_for(const Entry* entry, const char** text, const char** class_name) {
    static const char* const icon_image[] = {".png", ".jpg", ".bmp", ".gif", ".svg", NULL};
    static const char* const icon_code[] = {".cpp", ".h", ".c", ".py", ".js", ".rs", NULL};
    static const char* const class_image[] = {".png", ".jpg", ".bmp", ".gif", NULL};
    static const char* const class_code[] = {".cpp", ".h", ".c", ".py", ".js", NULL};
    static const char* const doc[] = {".txt", ".md", ".doc", ".pdf", NULL};
    char ext[32];
    if (entry->is_dir) {
        *text = "[D]";
        *class_name = "file-icon folder";
        return;
    }
    lower_extension(entry->name, ext, sizeof ext);
    *text = ext_in(ext, icon_image) ? "[I]" : ext_in(ext, icon_code) ? "[<>]"
          : ext_in(ext, doc) ? "[T]" : "[F]";
    *class_name = ext_in(ext, class_image) ? "file-icon image"
                : ext_in(ext, class_code) ? "file-icon code"
                : ext_in(ext, doc) ? "file-icon doc" : "file-icon file";
}

static void join(const char* dir, const char* name, char* out) {
    size_t length = strlen(dir);
    if (length && is_sep(dir[length - 1])) snprintf(out, PATH_CAP, "%s%s", dir, name);
    else snprintf(out, PATH_CAP, "%s%c%s", dir, SEP, name);
}

static int add_entry(Entry** entries, size_t* count, size_t* capacity, const char* name, int dir) {
    if (*count == *capacity) {
        size_t grown_capacity = *capacity ? *capacity * 2 : 32;
        Entry* grown = (Entry*)realloc(*entries, grown_capacity * sizeof **entries);
        if (!grown) return 0;
        *entries = grown;
        *capacity = grown_capacity;
    }
    (*entries)[*count].name = (char*)malloc(strlen(name) + 1);
    if (!(*entries)[*count].name) return 0;
    memcpy((*entries)[*count].name, name, strlen(name) + 1);
    (*entries)[*count].is_dir = dir;
    ++*count;
    return 1;
}

/* fs::directory_iterator: every entry but "." and ".."; errors give an empty list. */
static size_t list_directory(const char* dir, Entry** entries) {
    size_t count = 0, capacity = 0;
    *entries = NULL;
#ifdef _WIN32
    {
        char pattern[PATH_CAP];
        wchar_t wide[PATH_CAP];
        WIN32_FIND_DATAW data;
        HANDLE find;
        join(dir, "*", pattern);
        if (!MultiByteToWideChar(CP_UTF8, 0, pattern, -1, wide, PATH_CAP)) return 0;
        find = FindFirstFileW(wide, &data);
        if (find == INVALID_HANDLE_VALUE) return 0;
        do {
            char name[PATH_CAP];
            if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L"..")) continue;
            to_utf8(data.cFileName, name, sizeof name);
            if (!add_entry(entries, &count, &capacity, name,
                           (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)) break;
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
#else
    {
        DIR* handle = opendir(dir);
        struct dirent* item;
        if (!handle) return 0;
        while ((item = readdir(handle)) != NULL) {
            char full[PATH_CAP];
            struct stat info;
            if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
            join(dir, item->d_name, full);
            if (!add_entry(entries, &count, &capacity, item->d_name,
                           stat(full, &info) == 0 && S_ISDIR(info.st_mode))) break;
        }
        closedir(handle);
    }
#endif
    return count;
}

static int compare_entries(const void* a, const void* b) {
    const Entry* left = (const Entry*)a;
    const Entry* right = (const Entry*)b;
    if (left->is_dir != right->is_dir) return left->is_dir ? -1 : 1;
    return strcmp(left->name, right->name);
}

static void clear_children(KuiElement* parent) {
    KuiElement* child = NULL;
    while (kui_element_first_child(parent, &child) == KUI_OK && child) {
        kui_element_remove_child(parent, child);
        kui_element_release(child);
        child = NULL;
    }
}

/* createElement(tag) with class and text; target (may be NULL) goes into data-path. */
static KuiElement* make(const char* tag, const char* class_name, const char* text,
                        const char* target) {
    KuiElement* element = kuex_make_element(g_doc, tag, text, NULL);
    if (!element) return NULL;
    if (class_name) kui_element_set_attribute(element, "class", 0, class_name, 0);
    if (target) kui_element_set_attribute(element, "data-path", 0, target, 0);
    return element;
}

static void update_breadcrumb(const char* path) {
    KuiElement* bc = kuex_by_id(g_doc, "breadcrumb");
    size_t root = root_length(path), end;
    char segment[PATH_CAP];
    if (!bc) return;
    clear_children(bc);
    /* Root first (its text is the root itself, e.g. "C:\" or "/"), then one segment per name. */
    snprintf(segment, sizeof segment, "%.*s", (int)root, path);
    if (root) kuex_append_release(bc, make("span", "seg", segment, segment));
    for (end = root; path[end];) {
        size_t start = end;
        while (path[end] && !is_sep(path[end])) ++end;
        if (end > start) {
            char name[PATH_CAP];
            snprintf(segment, sizeof segment, "%.*s", (int)end, path);
            snprintf(name, sizeof name, "%.*s", (int)(end - start), path + start);
            if (start > 0) kuex_append_release(bc, make("span", "sep", "/", NULL));
            kuex_append_release(bc, make("span", "seg", name, segment));
        }
        while (path[end] && is_sep(path[end])) ++end;
    }
    kui_element_release(bc);
}

static void update_sidebar(void) {
    static const char* const links[][2] = {{"~", "Home"}, {"D", "Desktop"}, {"[>", "Documents"}};
    KuiElement* sidebar = kuex_by_id(g_doc, "sidebar");
    size_t i;
    if (!sidebar) return;
    clear_children(sidebar);
    kuex_append_release(sidebar, make("div", "sidebar-section", "Quick Access", NULL));
    for (i = 0; i < 3; ++i) {
        KuiElement* item = make("div", "sidebar-item", NULL, g_start);
        kuex_append_release(item, make("span", "sidebar-icon", links[i][0], NULL));
        kuex_append_release(item, make("span", NULL, links[i][1], NULL));
        kuex_append_release(sidebar, item);
    }
    kui_element_release(sidebar);
}

static void update_file_grid(const char* path) {
    KuiElement* grid = kuex_by_id(g_doc, "file-grid");
    Entry* entries;
    size_t count, i;
    if (!grid) return;
    clear_children(grid);
    count = list_directory(path, &entries);
    if (count) qsort(entries, count, sizeof *entries, compare_entries);
    if (!count) kuex_append_release(grid, make("div", "empty", "Empty folder", NULL));
    for (i = 0; i < count; ++i) {
        const char* icon;
        const char* icon_class;
        char target[PATH_CAP];
        KuiElement* item;
        join(path, entries[i].name, target);
        icon_for(&entries[i], &icon, &icon_class);
        item = make("div", "file-item", NULL, entries[i].is_dir ? target : NULL);
        kuex_append_release(item, make("div", icon_class, icon, NULL));
        kuex_append_release(item, make("div", "file-name", entries[i].name, NULL));
        kuex_append_release(grid, item);
        free(entries[i].name);
    }
    free(entries);
    kui_element_release(grid);
}

static void navigate_to(const char* path) {
    update_breadcrumb(path);
    update_file_grid(path);
}

static void on_click(KuiEvent* event, void* user_data) {
    KuiElement* target = NULL;
    KuiElement* item = NULL;
    char path[PATH_CAP];
    size_t length = 0;
    (void)user_data;
    if (kui_event_get_target(event, &target) != KUI_OK || !target) return;
    if (kui_element_closest(target, "[data-path]", 0, &item) == KUI_OK && item) {
        if (kui_element_get_attribute(item, "data-path", 0, path, sizeof path, &length) == KUI_OK) {
            path[sizeof path - 1] = '\0';
            navigate_to(path); /* item is removed here; only our own handle keeps it alive */
        }
        kui_element_release(item);
    }
    kui_element_release(target);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    const char* start = getenv("KUI_EXAMPLE_START_DIR");
    char cwd[PATH_CAP];
    g_doc = host->document;
    if (!start || !*start) {
#ifdef _WIN32
        wchar_t wide[PATH_CAP];
        if (!GetCurrentDirectoryW(PATH_CAP, wide)) wide[0] = L'\0';
        to_utf8(wide, cwd, sizeof cwd);
#else
        if (!getcwd(cwd, sizeof cwd)) cwd[0] = '\0';
#endif
        start = cwd;
    }
    normalise(start, g_start);
    if (kuex_on(g_doc, "breadcrumb", "click", on_click, NULL) |
        kuex_on(g_doc, "sidebar", "click", on_click, NULL) |
        kuex_on(g_doc, "file-grid", "click", on_click, NULL)) {
        return 1;
    }
    update_sidebar();
    navigate_to(g_start);
    return 0;
}
