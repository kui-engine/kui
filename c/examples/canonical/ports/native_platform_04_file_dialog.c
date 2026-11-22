/* Host logic of examples/native/platform/04_file_dialog/main.cpp: Open File / Save File / Select
 * Folder show the native dialogs and write the chosen path, or "... cancelled.", into #result. */
#include "kui_c_example_dom.h"

/* 32768 bytes covers the Windows path namespace (see kui_platform_open_file_dialog). */
static char g_path[32768];
static KuiDocument* g_doc;

static void show_result(KuiStatus status, const char* cancelled) {
    kuex_set_text(g_doc, "result", status == KUI_OK ? g_path : cancelled);
}

static void on_open(KuiEvent* event, void* user_data) {
    static const char* const filters[] = {"*.html", "*.css", "*.txt"};
    KuiCFileDialogOptions options;
    size_t length = 0;
    (void)event;
    (void)user_data;
    memset(&options, 0, sizeof options);
    options.title = "Open a document";
    options.filters = filters;
    options.filter_count = 3;
    show_result(kui_platform_open_file_dialog(&options, g_path, sizeof g_path, &length),
                "Open cancelled.");
}

static void on_save(KuiEvent* event, void* user_data) {
    static const char* const filters[] = {"*.html"};
    KuiCFileDialogOptions options;
    size_t length = 0;
    (void)event;
    (void)user_data;
    memset(&options, 0, sizeof options);
    options.title = "Save a document";
    options.default_path = "example.html";
    options.filters = filters;
    options.filter_count = 1;
    show_result(kui_platform_save_file_dialog(&options, g_path, sizeof g_path, &length),
                "Save cancelled.");
}

static void on_folder(KuiEvent* event, void* user_data) {
    size_t length = 0;
    (void)event;
    (void)user_data;
    show_result(kui_platform_select_folder_dialog("Select a folder", 0, g_path, sizeof g_path,
                                                  &length),
                "Folder selection cancelled.");
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    return kuex_on(g_doc, "openBtn", "click", on_open, NULL) |
           kuex_on(g_doc, "saveBtn", "click", on_save, NULL) |
           kuex_on(g_doc, "folderBtn", "click", on_folder, NULL);
}
