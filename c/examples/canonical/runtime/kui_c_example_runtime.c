/* Strict C11 (CMAKE_C_EXTENSIONS OFF) hides POSIX declarations in glibc: readlink,
 * clock_gettime and localtime_r need this before the first system header. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "kui_c_example_runtime.h"

#include <kui_c.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#define KUI_C_PATH_CAPACITY 32768

typedef struct ExampleRuntime {
    const KuiCExampleSpec* spec;
    KuiWindow* window;
    KuiWindow* second_window;
    KuiDocument* document;
    KuiDocument* second_document;
    KuiScriptEngine* script;
    KuiHttpClient* http;
    KuiElement* media;
    KuiElement* status;
} ExampleRuntime;

static ExampleRuntime g_runtime;

static int status_ok(KuiStatus status, const char* expression) {
    if (status == KUI_OK) return 1;
    fprintf(stderr, "[%s] %s failed: %s (%d)\n",
            g_runtime.spec ? g_runtime.spec->canonical_target : "c-example",
            expression, kui_status_message(status), (int)status);
    return 0;
}

#define REQUIRE(expression)                                                   \
    do {                                                                      \
        if (!status_ok((expression), #expression)) return 0;                  \
    } while (0)

static int file_exists(const char* path) {
    FILE* stream = fopen(path, "rb");
    if (!stream) return 0;
    fclose(stream);
    return 1;
}

static void path_directory(char* path) {
    char* slash = strrchr(path, '\\');
    char* forward = strrchr(path, '/');
    if (forward && (!slash || forward > slash)) slash = forward;
    if (slash) *slash = '\0';
}

static int executable_directory(char* output, size_t capacity) {
#ifdef _WIN32
    DWORD length = GetModuleFileNameA(NULL, output, (DWORD)capacity);
    if (length == 0 || length >= capacity) return 0;
    output[length] = '\0';
#elif defined(__APPLE__)
    uint32_t length = (uint32_t)capacity;
    if (_NSGetExecutablePath(output, &length) != 0) return 0;
#else
    ssize_t length = readlink("/proc/self/exe", output, capacity - 1);
    if (length <= 0 || (size_t)length >= capacity) return 0;
    output[length] = '\0';
#endif
    path_directory(output);
    return 1;
}

static int join_path(char* output, size_t capacity,
                     const char* left, const char* right) {
    int written = snprintf(output, capacity, "%s/%s", left, right);
    return written > 0 && (size_t)written < capacity;
}

static int find_asset(const char* relative, char* output, size_t capacity) {
    char directory[KUI_C_PATH_CAPACITY];
    char candidate[KUI_C_PATH_CAPACITY];
    const char* prefixes[] = {"assets", "", "../assets", "../../assets"};
    size_t index;

    if (!relative || !*relative) return 0;
    if (file_exists(relative)) {
        snprintf(output, capacity, "%s", relative);
        return 1;
    }
    if (!executable_directory(directory, sizeof directory)) return 0;
    for (index = 0; index < sizeof prefixes / sizeof prefixes[0]; ++index) {
        char base[KUI_C_PATH_CAPACITY];
        if (prefixes[index][0]) {
            if (!join_path(base, sizeof base, directory, prefixes[index])) continue;
        } else {
            snprintf(base, sizeof base, "%s", directory);
        }
        if (!join_path(candidate, sizeof candidate, base, relative)) continue;
        if (file_exists(candidate)) {
            snprintf(output, capacity, "%s", candidate);
            return 1;
        }
    }
    return 0;
}

static void update_text(KuiDocument* document, const char* id, const char* text) {
    KuiElement* element = NULL;
    if (kui_document_get_element_by_id(document, id, 0, &element) == KUI_OK) {
        kui_element_set_text_content(element, text, 0);
        kui_element_release(element);
    }
}

static void script_error(const char* error, size_t error_len,
                         const char* source, size_t source_len,
                         int line, void* user_data) {
    (void)user_data;
    fprintf(stderr, "[Lua] %.*s:%d: %.*s\n",
            (int)source_len, source, line, (int)error_len, error);
}

static int bind_script(ExampleRuntime* runtime) {
    REQUIRE(kui_script_engine_create(&runtime->script));
    REQUIRE(kui_script_engine_set_error_handler(runtime->script, script_error, NULL));
    REQUIRE(kui_script_engine_bind_document(runtime->script, runtime->document));
    REQUIRE(kui_window_set_script_engine(runtime->window, runtime->script));
    return 1;
}

static int init_extensions(ExampleRuntime* runtime) {
    char directory[KUI_C_PATH_CAPACITY];
    size_t loaded = 0;
    if (!executable_directory(directory, sizeof directory)) return 0;
    if (!status_ok(kui_document_init_extensions(
                       runtime->document, directory, 0, &loaded),
                   "kui_document_init_extensions")) {
        return 0;
    }
    fprintf(stderr, "[%s] extensions loaded=%zu from %s\n",
            runtime->spec->canonical_target, loaded, directory);
    return 1;
}

static int load_document(ExampleRuntime* runtime, const char* explicit_path) {
    char path[KUI_C_PATH_CAPACITY];
    const char* requested = explicit_path ? explicit_path : runtime->spec->asset;
    if (!find_asset(requested, path, sizeof path)) {
        fprintf(stderr, "[%s] asset not found: %s\n",
                runtime->spec->canonical_target, requested ? requested : "(null)");
        return 0;
    }
    REQUIRE(kui_document_load_html_file(runtime->document, path, 0));
    fprintf(stderr, "[%s] loaded %s\n", runtime->spec->canonical_target, path);
    return 1;
}

static KuiCExtensionValue ext_int(long long value) {
    KuiCExtensionValue result;
    memset(&result, 0, sizeof result);
    result.type = KUI_C_EXT_INT;
    result.int_value = value;
    return result;
}

static KuiCExtensionValue ext_float(double value) {
    KuiCExtensionValue result;
    memset(&result, 0, sizeof result);
    result.type = KUI_C_EXT_FLOAT;
    result.float_value = value;
    return result;
}

static long long invoke_integer(KuiDocument* document, KuiElement* element,
                                const char* method,
                                const KuiCExtensionValue* args, size_t count) {
    KuiExtensionResult* result = NULL;
    KuiCExtensionValue value;
    long long output = 0;
    memset(&value, 0, sizeof value);
    if (kui_document_extension_invoke(document, element, method, 0,
                                      args, count, 1, &result) != KUI_OK) {
        return 0;
    }
    if (kui_extension_result_get_value(result, 0, &value) == KUI_OK &&
        value.type == KUI_C_EXT_INT) {
        output = value.int_value;
    }
    kui_extension_result_release(result);
    return output;
}

static void invoke_void(KuiDocument* document, KuiElement* element,
                        const char* method,
                        const KuiCExtensionValue* args, size_t count) {
    KuiExtensionResult* result = NULL;
    if (kui_document_extension_invoke(document, element, method, 0,
                                      args, count, 0, &result) == KUI_OK && result) {
        kui_extension_result_release(result);
    }
}

static void setup_three_d(ExampleRuntime* runtime) {
    static const char* methods[] = {
        "createTriangle", "createBox", "createSphere", "createBox", "createSphere",
        "createBox", "createBox", "createBox", "createSphere", "createSphere",
        "createBox", "createSphere", "createBox", "createBox", "createSphere",
        "createBox", "createBox", "createBox", "createSphere", "createSphere",
        "createPlane", "createBox", "createBox", "createSphere", "createBox", "createSphere"
    };
    KuiElement* view = NULL;
    KuiCExtensionValue color[3];
    KuiCExtensionValue entity_arg[1];
    long long entity;
    int index = runtime->spec->variant - 1;
    int loaded = 0;
    if (index < 0 || index >= 26) return;
    if (kui_document_extension_is_loaded(runtime->document, "kui-3d-view", 0,
                                         &loaded) != KUI_OK || !loaded) {
        fprintf(stderr, "[%s] kui_ext_3d is not loaded\n",
                runtime->spec->canonical_target);
        return;
    }
    if (kui_document_get_element_by_id(runtime->document, "view", 0, &view) != KUI_OK) return;
    color[0] = ext_float(0.18 + (index % 3) * 0.20);
    color[1] = ext_float(0.45 + (index % 2) * 0.25);
    color[2] = ext_float(0.92 - (index % 4) * 0.13);
    entity = invoke_integer(runtime->document, view, methods[index], color, 3);
    if (entity) {
        entity_arg[0] = ext_int(entity);
        invoke_void(runtime->document, view, "addToScene", entity_arg, 1);
    }
    fprintf(stderr, "[%s] 3D route=%d method=%s entity=%lld\n",
            runtime->spec->canonical_target, runtime->spec->variant,
            methods[index], entity);
    kui_element_release(view);
}

static void on_media_play(KuiEvent* event, void* user_data) {
    (void)event;
    kui_media_play((KuiElement*)user_data);
}

static void on_media_pause(KuiEvent* event, void* user_data) {
    (void)event;
    kui_media_pause((KuiElement*)user_data);
}

static void setup_media(ExampleRuntime* runtime) {
    char path[KUI_C_PATH_CAPACITY];
    KuiElement* button = NULL;
    KuiListenerId listener = 0;
    int available = 0;
    const char* asset = runtime->spec->variant == 1
        ? "native_media/sample.mp3" : "native_media/sample.mp4";
    if (kui_document_get_element_by_id(runtime->document, "media", 0,
                                       &runtime->media) != KUI_OK) return;
    kui_media_set_controls(runtime->media, 1);
    kui_media_set_volume(runtime->media, 0.75f);
    if (find_asset(asset, path, sizeof path)) kui_media_set_src(runtime->media, path, 0);
    kui_media_is_available(runtime->media, &available);
    update_text(runtime->document, "status",
                available ? "Media backend available through C ABI."
                          : "Media backend unavailable; deploy the media runtime DLLs.");
    if (kui_document_get_element_by_id(runtime->document, "play", 0, &button) == KUI_OK) {
        kui_element_add_event_listener(button, "click", 0, on_media_play,
                                       runtime->media, &listener);
        kui_element_release(button);
    }
    if (kui_document_get_element_by_id(runtime->document, "pause", 0, &button) == KUI_OK) {
        kui_element_add_event_listener(button, "click", 0, on_media_pause,
                                       runtime->media, &listener);
        kui_element_release(button);
    }
}

static void show_http_response(KuiHttpResponse* response, void* user_data) {
    ExampleRuntime* runtime = (ExampleRuntime*)user_data;
    size_t length = 0;
    int code = 0;
    char* body;
    char status[128];
    if (!response) return;
    kui_http_response_get_status(response, &code);
    if (kui_http_response_get_body(response, NULL, 0, &length) != KUI_OK) {
        kui_http_response_release(response);
        return;
    }
    if (length > 8192) length = 8192;
    body = (char*)malloc(length + 1);
    if (body) {
        size_t actual = 0;
        if (kui_http_response_get_body(response, (unsigned char*)body,
                                       length + 1, &actual) == KUI_OK) {
            body[actual < length + 1 ? actual : length] = '\0';
            update_text(runtime->document, "response", body);
        }
        free(body);
    }
    snprintf(status, sizeof status, "HTTP %d via the KUI C ABI", code);
    update_text(runtime->document, "status", status);
    kui_http_response_release(response);
}

static void on_http_send(KuiEvent* event, void* user_data) {
    ExampleRuntime* runtime = (ExampleRuntime*)user_data;
    KuiElement* input = NULL;
    char url[2048] = "https://example.com/";
    size_t length = 0;
    KuiHttpResponse* response = NULL;
    (void)event;
    if (kui_document_get_element_by_id(runtime->document, "url", 0, &input) == KUI_OK) {
        kui_element_get_attribute(input, "value", 0, url, sizeof url, &length);
        kui_element_release(input);
    }
    update_text(runtime->document, "status", "Request in progress…");
    if (runtime->spec->variant == 2) {
        kui_http_client_fetch_async(runtime->http, url, 0, 15.0,
                                    show_http_response, runtime);
    } else if (kui_http_client_fetch_sync(runtime->http, url, 0, 15.0,
                                          &response) == KUI_OK) {
        show_http_response(response, runtime);
    }
}

static void setup_network(ExampleRuntime* runtime) {
    KuiElement* button = NULL;
    KuiListenerId listener = 0;
    if (!status_ok(kui_http_client_create(&runtime->http),
                   "kui_http_client_create")) return;
    kui_http_client_set_user_agent(runtime->http, "kui-c-example-port/1.0", 0);
    if (kui_document_get_element_by_id(runtime->document, "send", 0, &button) == KUI_OK) {
        kui_element_add_event_listener(button, "click", 0, on_http_send,
                                       runtime, &listener);
        kui_element_release(button);
    }
}

static void on_extension_action(KuiEvent* event, void* user_data) {
    ExampleRuntime* runtime = (ExampleRuntime*)user_data;
    KuiElement* element = NULL;
    KuiExtensionResult* result = NULL;
    KuiCExtensionValue argument;
    char output[1024];
    size_t length = 0;
    (void)event;
    if (kui_document_get_element_by_id(runtime->document, "hello", 0, &element) != KUI_OK) return;
    memset(&argument, 0, sizeof argument);
    argument.type = KUI_C_EXT_STRING;
    argument.string_value = "C ABI";
    if (kui_document_extension_invoke(runtime->document, element, "greet", 0,
                                      &argument, 1, 1, &result) == KUI_OK && result) {
        if (kui_extension_result_get_string(result, 0, output, sizeof output,
                                            &length) == KUI_OK) {
            update_text(runtime->document, "status", output);
        }
        kui_extension_result_release(result);
    }
    kui_element_release(element);
}

static void setup_hello_extension(ExampleRuntime* runtime) {
    KuiElement* button = NULL;
    KuiListenerId listener = 0;
    int loaded = 0;
    kui_document_extension_is_loaded(runtime->document, "kui-hello", 0, &loaded);
    update_text(runtime->document, "status",
                loaded ? "kui_ext_hello loaded through C ABI."
                       : "kui_ext_hello was not found beside this executable.");
    if (kui_document_get_element_by_id(runtime->document, "action", 0, &button) == KUI_OK) {
        kui_element_add_event_listener(button, "click", 0, on_extension_action,
                                       runtime, &listener);
        kui_element_release(button);
    }
}

static void setup_lottie(ExampleRuntime* runtime) {
    char path[KUI_C_PATH_CAPACITY];
    KuiElement* element = NULL;
    if (!find_asset("native_animation/pulse.json", path, sizeof path)) return;
    if (kui_document_query_selector(runtime->document, "lottie-player", 0,
                                    &element) == KUI_OK) {
        kui_lottie_set_src(element, path, 0);
        kui_lottie_play(element);
        kui_element_release(element);
    }
}

static void setup_pdf(ExampleRuntime* runtime) {
    KuiCPDFOptions options;
    int written = 0;
    memset(&options, 0, sizeof options);
    options.title = runtime->spec->title;
    options.author = "KUI C ABI example port";
    options.page_width_pt = 612.0f;
    options.page_height_pt = 792.0f;
    options.generate_outline = 1;
    if (kui_document_print_to_pdf(runtime->document, "kui-c-example.pdf", 0,
                                  &options, &written) == KUI_OK) {
        fprintf(stderr, "[%s] PDF written=%d\n",
                runtime->spec->canonical_target, written);
    }
}

/* -- kui_tray_icon_example: the tray behaviour of examples/tray_icon_example ----------- */
enum {
    TRAY_MENU_TOGGLE = 1, TRAY_MENU_INFO, TRAY_MENU_WARN, TRAY_MENU_ERROR, TRAY_MENU_ABOUT,
    TRAY_MENU_QUIT
};
typedef struct TrayApp {
    int clicks, doubles, rights, balloon_clicks, balloons_sent, tooltip_variant;
    int tray_visible, close_hinted;
} TrayApp;
static TrayApp g_tray;
static const char* const k_tray_tooltips[3] = {
    "KUI Tray Icon Example \xE2\x80\x94 Idle",
    "KUI Tray Icon Example \xE2\x80\x94 Syncing\xE2\x80\xA6",
    "KUI Tray Icon Example \xE2\x80\x94 All done \xE2\x9C\x93"};
static const char* const k_tray_labels[3] = {"Idle", "Busy", "OK"};

static void tray_timestamp(char* out, size_t capacity) {
#ifdef _WIN32
    SYSTEMTIME now;
    GetLocalTime(&now);
    snprintf(out, capacity, "%02d:%02d:%02d.%03d", now.wHour, now.wMinute, now.wSecond,
             now.wMilliseconds);
#else
    struct timespec now;
    struct tm local;
    clock_gettime(CLOCK_REALTIME, &now);
    localtime_r(&now.tv_sec, &local);
    snprintf(out, capacity, "%02d:%02d:%02d.%03ld", local.tm_hour, local.tm_min, local.tm_sec,
             now.tv_nsec / 1000000L);
#endif
}

/* Newest entry first, at most 10 (as the original). */
static void tray_log(const char* message) {
    KuiElement* list = NULL;
    KuiElement* item = NULL;
    KuiElement* first = NULL;
    char line[256];
    char stamp[32];
    size_t count = 0;
    if (kui_document_get_element_by_id(g_runtime.document, "logList", 0, &list) != KUI_OK) return;
    tray_timestamp(stamp, sizeof stamp);
    snprintf(line, sizeof line, "%s  %s", stamp, message);
    if (kui_document_create_element(g_runtime.document, "li", 0, &item) == KUI_OK) {
        kui_element_set_text_content(item, line, 0);
        kui_element_first_child(list, &first);
        kui_element_insert_before(list, item, first);
        if (first) kui_element_release(first);
        kui_element_release(item);
    }
    while (kui_element_child_count(list, &count) == KUI_OK && count > 10) {
        KuiElement* last = NULL;
        if (kui_element_last_child(list, &last) != KUI_OK || !last) break;
        kui_element_remove_child(list, last);
        kui_element_release(last);
    }
    kui_element_release(list);
}

static void tray_counter(const char* id, int value) {
    char text[32];
    snprintf(text, sizeof text, "%d", value);
    update_text(g_runtime.document, id, text);
}

static int window_visible(void) {
    int visible = 0;
    kui_window_is_visible(g_runtime.window, &visible);
    return visible;
}

static void tray_toast(KuiCToastLevel level, const char* message) {
    KuiCToastOptions options;
    if (!window_visible()) return;
    memset(&options, 0, sizeof options);
    options.message = message;
    options.level = level;
    options.position = "top-right";
    options.duration_ms = 2500;
    kui_document_show_toast(g_runtime.document, &options, NULL);
}

static void tray_show_window(void) {
    kui_window_show(g_runtime.window);
    kui_window_focus(g_runtime.window);
}

static void tray_balloon(int icon_type, const char* title, const char* body) {
    char line[160];
    ++g_tray.balloons_sent;
    snprintf(line, sizeof line, "[menu] Balloon: %s", title);
    tray_log(line);
    kui_window_tray_show_balloon(g_runtime.window, title, 0, body, 0, icon_type, 4000);
}

static void tray_apply_tooltip(void) {
    kui_window_tray_set_tooltip(g_runtime.window, k_tray_tooltips[g_tray.tooltip_variant], 0);
    update_text(g_runtime.document, "tooltipLabel", k_tray_labels[g_tray.tooltip_variant]);
}

static void tray_update_badge(void) {
    KuiElement* badge = NULL;
    if (kui_document_get_element_by_id(g_runtime.document, "statusBadge", 0, &badge) == KUI_OK) {
        kui_element_set_class_name(badge, g_tray.tray_visible ? "status-dot" : "status-dot off", 0);
        kui_element_release(badge);
    }
    update_text(g_runtime.document, "statusText", g_tray.tray_visible ? "active" : "hidden");
}

static void tray_toggle_window(void) {
    if (window_visible()) {
        tray_log("[menu] Hide window");
        kui_window_hide(g_runtime.window);
    } else {
        tray_log("[menu] Show window");
        tray_show_window();
    }
}

static void tray_event(KuiCTrayEvent event, void* user_data) {
    (void)user_data;
    switch (event) {
        case KUI_C_TRAY_CLICK:
            tray_counter("cntClick", ++g_tray.clicks);
            tray_log("[tray] left-click");
            tray_toast(KUI_C_TOAST_INFO, "Tray: left-click");
            if (window_visible()) kui_window_hide(g_runtime.window);
            else tray_show_window();
            break;
        case KUI_C_TRAY_DOUBLE_CLICK:
            tray_counter("cntDouble", ++g_tray.doubles);
            tray_log("[tray] double-click (show + focus)");
            tray_show_window();
            tray_toast(KUI_C_TOAST_SUCCESS, "Window restored");
            break;
        case KUI_C_TRAY_RIGHT_CLICK:
            tray_counter("cntRight", ++g_tray.rights);
            tray_log("[tray] right-click (context menu)");
            break;
        case KUI_C_TRAY_BALLOON_CLICK:
            tray_counter("cntBalloon", ++g_tray.balloon_clicks);
            tray_log("[tray] balloon-click (restore window)");
            tray_show_window();
            tray_toast(KUI_C_TOAST_INFO, "Balloon clicked");
            break;
    }
}

static void tray_quit(void) {
    tray_log("[menu] Quit requested");
    g_tray.close_hinted = 1;  /* the close that follows really exits */
    kui_window_tray_destroy(g_runtime.window);
    kui_window_close(g_runtime.window);
    kui_platform_quit(0);
}

static void tray_menu(uint32_t id, void* user_data) {
    (void)user_data;
    switch (id) {
        case TRAY_MENU_TOGGLE: tray_toggle_window(); break;
        case TRAY_MENU_INFO: tray_balloon(1, "KUI", "Hello from system tray!"); break;
        case TRAY_MENU_WARN: tray_balloon(2, "Heads up", "Something may need attention."); break;
        case TRAY_MENU_ERROR: tray_balloon(3, "Error", "An example error has occurred."); break;
        case TRAY_MENU_ABOUT:
            tray_log("[menu] About");
            kui_window_tray_show_balloon(g_runtime.window, "About KUI Tray Example", 0,
                                         "KUI Framework v1.x \xC2\xB7 ENH-P1-003 System Tray Icon \xC2\xB7 \xC2\xA9 2026",
                                         0, 1, 5000);
            break;
        case TRAY_MENU_QUIT: tray_quit(); break;
        default: break;
    }
}

static void tray_button(KuiEvent* event, void* user_data) {
    const char* id = (const char*)user_data;
    (void)event;
    if (strcmp(id, "btnBalloonInfo") == 0) {
        tray_balloon(1, "Info from app", "This balloon was sent by the in-app button.");
    } else if (strcmp(id, "btnBalloonWarn") == 0) {
        tray_balloon(2, "Warning from app", "Remember to save your work.");
    } else if (strcmp(id, "btnBalloonError") == 0) {
        tray_balloon(3, "Error from app", "Simulated error for demonstration.");
    } else if (strcmp(id, "btnToggleTooltip") == 0) {
        g_tray.tooltip_variant = (g_tray.tooltip_variant + 1) % 3;
        tray_apply_tooltip();
        tray_log("[ui] tooltip changed");
    } else if (strcmp(id, "btnToggleVisible") == 0) {
        g_tray.tray_visible = !g_tray.tray_visible;
        kui_window_tray_set_visible(g_runtime.window, g_tray.tray_visible);
        tray_update_badge();
        update_text(g_runtime.document, "btnToggleVisible", g_tray.tray_visible ? "Hide Tray" : "Show Tray");
        tray_log(g_tray.tray_visible ? "[ui] tray shown" : "[ui] tray hidden");
    }
}

/* The first close hides to the tray with a balloon; the second really closes. */
static void tray_close(KuiEvent* event, void* user_data) {
    (void)user_data;
    if (!g_tray.close_hinted) {
        g_tray.close_hinted = 1;
        kui_event_prevent_default(event);
        kui_window_hide(g_runtime.window);
        kui_window_tray_show_balloon(
            g_runtime.window, "Still running", 0,
            "KUI Tray Example was minimized to the system tray. Double-click the icon to restore, "
            "or right-click for Quit.", 0, 1, 5000);
        tray_log("[close] minimized to tray (first close)");
        return;
    }
    kui_window_tray_destroy(g_runtime.window);
}

static int setup_tray(ExampleRuntime* runtime) {
    static const char* const buttons[] = {"btnBalloonInfo", "btnBalloonWarn", "btnBalloonError",
                                          "btnToggleTooltip", "btnToggleVisible"};
    static const KuiCMenuItem items[] = {
        {"Show / Hide Window", 0, NULL, 0, TRAY_MENU_TOGGLE, 0, 0, 0, 0, NULL, 0},
        {NULL, 0, NULL, 0, 0, 1, 0, 0, 0, NULL, 0},
        {"Send Info Balloon", 0, NULL, 0, TRAY_MENU_INFO, 0, 0, 0, 0, NULL, 0},
        {"Send Warning Balloon", 0, NULL, 0, TRAY_MENU_WARN, 0, 0, 0, 0, NULL, 0},
        {"Send Error Balloon", 0, NULL, 0, TRAY_MENU_ERROR, 0, 0, 0, 0, NULL, 0},
        {NULL, 0, NULL, 0, 0, 1, 0, 0, 0, NULL, 0},
        {"About\xE2\x80\xA6", 0, NULL, 0, TRAY_MENU_ABOUT, 0, 0, 0, 0, NULL, 0},
        {NULL, 0, NULL, 0, 0, 1, 0, 0, 0, NULL, 0},
        {"Quit", 0, NULL, 0, TRAY_MENU_QUIT, 0, 0, 0, 0, NULL, 0}};
    size_t index;
    memset(&g_tray, 0, sizeof g_tray);
    g_tray.tray_visible = 1;
    if (!status_ok(kui_window_tray_create(runtime->window, "", 0, k_tray_tooltips[0], 0, 1),
                   "kui_window_tray_create")) {
        fprintf(stderr, "[%s] failed to register tray icon - system tray may be disabled.\n",
                runtime->spec->canonical_target);
        return 0;
    }
    kui_window_tray_set_event_callback(runtime->window, tray_event, NULL);
    kui_window_tray_set_context_menu(runtime->window, items, sizeof items / sizeof items[0],
                                     tray_menu, NULL);
    tray_log("[init] tray icon installed");
    for (index = 0; index < sizeof buttons / sizeof buttons[0]; ++index) {
        KuiElement* button = NULL;
        if (kui_document_get_element_by_id(runtime->document, buttons[index], 0, &button) == KUI_OK) {
            kui_element_add_event_listener(button, "click", 0, tray_button, (void*)buttons[index], NULL);
            kui_element_release(button);
        }
    }
    kui_window_add_event_listener(runtime->window, "close", 0, tray_close, NULL, 0, NULL);
    tray_log("[init] ready");
    return 1;
}

/* -- kui_multi_window_animation: closing any window closes the others ----------------- */
static int g_closing_all = 0;

static void close_together(KuiEvent* event, void* user_data) {
    KuiWindow* self = (KuiWindow*)user_data;
    (void)event;
    if (g_closing_all) return;
    g_closing_all = 1;
    if (g_runtime.window && g_runtime.window != self) kui_window_close(g_runtime.window);
    if (g_runtime.second_window && g_runtime.second_window != self)
        kui_window_close(g_runtime.second_window);
}

static int parse_file_host(const KuiCExampleSpec* spec, int argc, char** argv,
                           const char** path, int* width, int* height, int* port) {
    int index;
    *path = NULL;
    *width = spec->width;
    *height = spec->height;
    *port = 9300;
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--file") == 0 && index + 1 < argc) *path = argv[++index];
        else if (strcmp(argv[index], "--width") == 0 && index + 1 < argc) *width = atoi(argv[++index]);
        else if (strcmp(argv[index], "--height") == 0 && index + 1 < argc) *height = atoi(argv[++index]);
        else if (strcmp(argv[index], "--port") == 0 && index + 1 < argc) *port = atoi(argv[++index]);
        else if (argv[index][0] != '-' && !*path) *path = argv[index];
    }
    if (!*path) {
        *path = spec->asset;
        fprintf(stderr,
                "[%s] no --file supplied; opening the packaged C host page. "
                "Use --file <html> for ADVEX automation.\n",
                spec->canonical_target);
    }
    return 1;
}

static void cleanup_runtime(ExampleRuntime* runtime) {
    if (runtime->http) {
        kui_http_client_cancel_all(runtime->http);
        kui_http_client_release(runtime->http);
    }
    if (runtime->media) kui_element_release(runtime->media);
    if (runtime->status) kui_element_release(runtime->status);
    if (runtime->script) kui_script_engine_release(runtime->script);
    if (runtime->second_document) kui_document_release(runtime->second_document);
    if (runtime->document) kui_document_release(runtime->document);
    if (runtime->second_window) kui_window_release(runtime->second_window);
    if (runtime->window) kui_window_release(runtime->window);
    kui_platform_shutdown();
    memset(runtime, 0, sizeof *runtime);
}

static void fill_host(KuiCExampleHost* host, int argc, char** argv) {
    host->window = g_runtime.window;
    host->document = g_runtime.document;
    host->second_window = g_runtime.second_window;
    host->second_document = g_runtime.second_document;
    host->script = g_runtime.script;
    host->argc = argc;
    host->argv = argv;
}

int kui_c_example_run(const KuiCExampleSpec* spec, int argc, char** argv) {
    KuiWindowProps props;
    const char* explicit_path = NULL;
    int width;
    int height;
    int devtools_port = 0;
    int exit_code = 0;
    int started = 0;
    int load_ok;

    if (!spec || !spec->canonical_target || !spec->title) return 2;
    memset(&g_runtime, 0, sizeof g_runtime);
    g_runtime.spec = spec;
    width = spec->width;
    height = spec->height;
    if (spec->kind == KUI_C_EXAMPLE_FILE_HOST &&
        !parse_file_host(spec, argc, argv, &explicit_path,
                         &width, &height, &devtools_port)) {
        return 2;
    }
    /* html_loader: the command line belongs to the example's prepare hook (sample name, --file,
     * window shape ...); the runtime always loads the packaged page first. */

    memset(&props, 0, sizeof props);
    props.title = spec->title;
    props.width = width;
    props.height = height;
    props.centered = 1;
    props.frameless = (spec->flags & KUI_C_EXAMPLE_FRAMELESS) != 0;
    props.no_decorations = props.frameless;
    props.transparent = (spec->flags & KUI_C_EXAMPLE_TRANSPARENT) != 0;
    props.always_on_top = (spec->flags & KUI_C_EXAMPLE_ALWAYS_ON_TOP) != 0;
    props.frameless_shadow = (spec->flags & KUI_C_EXAMPLE_FRAMELESS_SHADOW) != 0;
    if (spec->prepare) {
        int prepare_exit = 0;
        if (spec->prepare(argc, argv, &props, &prepare_exit) != 0) {
            fflush(stdout);
            return prepare_exit;
        }
    }

    if (!status_ok(kui_platform_init(), "kui_platform_init")) return 3;
    if (!status_ok(kui_window_create(&props, &g_runtime.window),
                   "kui_window_create")) {
        kui_platform_shutdown();
        return 4;
    }
    if (!status_ok(kui_window_get_document(g_runtime.window, &g_runtime.document),
                   "kui_window_get_document")) {
        cleanup_runtime(&g_runtime);
        return 5;
    }
    if (spec->flags & KUI_C_EXAMPLE_FRAMELESS_SIZEABLE) {
        kui_window_set_frameless_sizeable(g_runtime.window, 1);
    }
    if (spec->kind == KUI_C_EXAMPLE_HTML_LOADER) {
        /* As examples/html_loader/main.cpp: browser defaults, built-in components, popups
         * in their own native windows. */
        kui_document_set_style_profile(g_runtime.document, KUI_C_STYLE_BROWSER_COMPAT);
        kui_document_register_builtin_components(g_runtime.document);
        kui_document_set_popup_mode(g_runtime.document, KUI_C_POPUP_MODE_NATIVE_WINDOW);
    }
    if (!bind_script(&g_runtime)) {
        cleanup_runtime(&g_runtime);
        return 6;
    }
    if (spec->flags & KUI_C_EXAMPLE_EXTENSIONS) init_extensions(&g_runtime);
    if (spec->before_load) {
        KuiCExampleHost host;
        fill_host(&host, argc, argv);
        if (spec->before_load(&host) != 0) {
            fprintf(stderr, "[%s] example before-load code failed\n", spec->canonical_target);
            cleanup_runtime(&g_runtime);
            return 9;
        }
    }
    load_ok = load_document(&g_runtime, explicit_path);
    if (!load_ok) {
        cleanup_runtime(&g_runtime);
        return 7;
    }
    kui_script_engine_execute_scripts(g_runtime.script);

    {
        /* Test hook shared with the Go and Rust runners: KUI_EXAMPLE_DEVTOOLS_PORT=<port>
         * starts DevTools (also for examples that start it themselves, overriding their port)
         * so scripts/verify_example_ports.py can click and read the DOM. */
        const char* env_port = getenv("KUI_EXAMPLE_DEVTOOLS_PORT");
        int from_env = env_port && *env_port;
        if (from_env) devtools_port = atoi(env_port);
        if ((spec->flags & KUI_C_EXAMPLE_DEVTOOLS) || from_env) {
            kui_window_start_devtools(g_runtime.window, (unsigned short)devtools_port,
                                      &started);
            fprintf(stderr, "[%s] devtools started=%d\n",
                    spec->canonical_target, started);
        }
    }
    if (spec->flags & KUI_C_EXAMPLE_MULTIWINDOW) {
        KuiWindowProps second = props;
        second.title = "KUI C — second window";
        second.centered = 0;
        if (kui_window_create(&second, &g_runtime.second_window) == KUI_OK &&
            kui_window_get_document(g_runtime.second_window,
                                    &g_runtime.second_document) == KUI_OK) {
            kui_document_load_html(g_runtime.second_document,
                "<html><body style='padding:32px;background:#172554;color:white;font-family:sans-serif'>"
                "<h2>Second window through the C ABI</h2></body></html>", 0);
            kui_window_set_position(g_runtime.second_window, 80, 80);
        }
        if ((spec->flags & KUI_C_EXAMPLE_CLOSE_TOGETHER) && g_runtime.second_window) {
            kui_window_add_event_listener(g_runtime.window, "close", 0, close_together,
                                          g_runtime.window, 0, NULL);
            kui_window_add_event_listener(g_runtime.second_window, "close", 0, close_together,
                                          g_runtime.second_window, 0, NULL);
        }
    }
    if ((spec->flags & KUI_C_EXAMPLE_TRAY) && !setup_tray(&g_runtime)) {
        cleanup_runtime(&g_runtime);
        return 1;
    }

    switch (spec->kind) {
        case KUI_C_EXAMPLE_THREE_D: setup_three_d(&g_runtime); break;
        case KUI_C_EXAMPLE_MEDIA: setup_media(&g_runtime); break;
        case KUI_C_EXAMPLE_NETWORK: setup_network(&g_runtime); break;
        case KUI_C_EXAMPLE_HELLO_EXTENSION: setup_hello_extension(&g_runtime); break;
        default: break;
    }
    if (spec->flags & KUI_C_EXAMPLE_LOTTIE) setup_lottie(&g_runtime);
    if (spec->flags & KUI_C_EXAMPLE_PDF) setup_pdf(&g_runtime);

    if (spec->ready) {
        KuiCExampleHost host;
        fill_host(&host, argc, argv);
        if (spec->ready(&host) != 0) {
            fprintf(stderr, "[%s] example host code failed\n", spec->canonical_target);
            cleanup_runtime(&g_runtime);
            return 9;
        }
    }

    if (!status_ok(kui_platform_run(&exit_code), "kui_platform_run")) exit_code = 8;
    cleanup_runtime(&g_runtime);
    return exit_code;
}
