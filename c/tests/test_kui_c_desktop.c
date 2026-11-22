/* Section 10 (3.61.8) regression tests: native menu bar, tray icon, keyboard
 * shortcuts, toasts and screenshots. C11. Creates one real window.
 * Menu activation and shortcut keys need real OS input (WM_COMMAND / key
 * state), and a balloon would pop a real system notification, so those paths
 * are checked through their definitions, ids and argument validation only;
 * tray callbacks are driven with kui_window_tray_simulate_event. */
#include <kui_c.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int passed = 0;
static int failed = 0;

#define CHECK(condition, message)                                             \
    do {                                                                       \
        if (condition) ++passed;                                               \
        else { ++failed; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, message); } \
    } while (0)

#define CHECK_OK(expression, message) CHECK((expression) == KUI_OK, message)

static int tray_events[4];
static void on_tray(KuiCTrayEvent event, void* user_data) {
    (void)user_data;
    if (event >= 0 && event < 4) ++tray_events[event];
}
static void on_menu(uint32_t id, void* user_data) {
    (void)id;
    (void)user_data;
}
static void on_shortcut(void* user_data) { (void)user_data; }

static void test_menu(KuiWindow* window) {
    KuiCMenuItem recent[2];
    memset(recent, 0, sizeof recent);
    recent[0].text = "a.txt"; recent[0].id = 31;
    recent[1].text = "b.txt"; recent[1].id = 32;
    KuiCMenuItem file[4];
    memset(file, 0, sizeof file);
    file[0].text = "&Open"; file[0].shortcut_label = "Ctrl+O"; file[0].id = 1;
    file[1].separator = 1;
    file[2].text = "Recent"; file[2].children = recent; file[2].child_count = 2;
    file[3].text = "Word wrap"; file[3].id = 2; file[3].checkable = 1;
    KuiCMenu menus[1];
    memset(menus, 0, sizeof menus);
    menus[0].text = "&File"; menus[0].items = file; menus[0].item_count = 4;

    CHECK_OK(kui_window_set_menu_bar(window, menus, 1, on_menu, NULL), "set menu bar");
    CHECK_OK(kui_window_menu_set_checked(window, 2, 1), "set checked");
    CHECK_OK(kui_window_menu_set_enabled(window, 32, 0), "disable nested item");
    CHECK(kui_window_menu_set_checked(window, 999, 1) == KUI_ERR_NOT_FOUND, "unknown item id");
    file[0].text = NULL;
    CHECK(kui_window_set_menu_bar(window, menus, 1, on_menu, NULL) == KUI_ERR_INVALID_ARG,
          "item without text is rejected");
    CHECK_OK(kui_window_clear_menu_bar(window), "clear menu bar");
    CHECK(kui_window_menu_set_checked(window, 2, 1) == KUI_ERR_NOT_FOUND, "cleared menu has no items");
}

static void test_tray(KuiWindow* window) {
    const KuiStatus created = kui_window_tray_create(window, NULL, 0, "kui c tray", 0, 1);
    CHECK(created == KUI_OK || created == KUI_ERR_NOT_SUPPORTED, "tray create");
    if (created != KUI_OK) return;
    int value = 0;
    CHECK_OK(kui_window_tray_is_active(window, &value), "tray is_active");
    CHECK(value == 1, "tray active");
    CHECK_OK(kui_window_tray_set_tooltip(window, "updated", 0), "tray tooltip");
    CHECK_OK(kui_window_tray_set_visible(window, 0), "tray hide");
    kui_window_tray_is_visible(window, &value);
    CHECK(value == 0, "tray hidden");
    CHECK_OK(kui_window_tray_set_visible(window, 1), "tray show");

    KuiCMenuItem items[2];
    memset(items, 0, sizeof items);
    items[0].text = "Show"; items[0].id = 7;
    items[1].text = "Quit"; items[1].id = 8;
    CHECK_OK(kui_window_tray_set_context_menu(window, items, 2, on_menu, NULL), "tray context menu");
    CHECK(kui_window_tray_show_balloon(window, "t", 0, "m", 0, 9, 1000) == KUI_ERR_INVALID_ARG,
          "balloon validates icon type");

    CHECK_OK(kui_window_tray_set_event_callback(window, on_tray, NULL), "tray callback");
    kui_window_tray_simulate_event(window, KUI_C_TRAY_CLICK);
    kui_window_tray_simulate_event(window, KUI_C_TRAY_DOUBLE_CLICK);
    kui_window_tray_simulate_event(window, KUI_C_TRAY_RIGHT_CLICK);
    kui_window_tray_simulate_event(window, KUI_C_TRAY_BALLOON_CLICK);
    CHECK(tray_events[0] == 1 && tray_events[1] == 1 && tray_events[3] == 1,
          "tray events reach the callback");
    CHECK(kui_window_tray_simulate_event(window, (KuiCTrayEvent)9) == KUI_ERR_INVALID_ARG,
          "simulate validates event");
    CHECK_OK(kui_window_tray_set_event_callback(window, NULL, NULL), "clear tray callback");
    kui_window_tray_simulate_event(window, KUI_C_TRAY_CLICK);
    CHECK(tray_events[0] == 1, "cleared callback is not called");
    CHECK_OK(kui_window_tray_destroy(window), "tray destroy");
    kui_window_tray_is_active(window, &value);
    CHECK(value == 0, "tray inactive after destroy");
}

static void test_shortcuts(KuiWindow* window) {
    uint32_t first = 0, again = 0, other = 0;
    CHECK_OK(kui_window_bind_shortcut(window, "Ctrl+S", 0, on_shortcut, NULL, &first), "bind");
    CHECK(first != 0, "bind yields an id");
    CHECK_OK(kui_window_bind_shortcut(window, "Ctrl+S", 0, on_shortcut, NULL, &again), "rebind");
    CHECK(again == first, "rebinding the same combination keeps its id");
    CHECK_OK(kui_window_bind_shortcut(window, "Ctrl+Shift+F5", 0, on_shortcut, NULL, &other),
             "bind second");
    CHECK(other != first, "different combination, different id");
    CHECK(kui_window_bind_shortcut(window, "", 0, on_shortcut, NULL, &other) == KUI_ERR_INVALID_ARG,
          "empty shortcut rejected");
    CHECK_OK(kui_window_unbind_shortcut(window, first), "unbind");
    CHECK_OK(kui_window_set_shortcuts_enabled(window, 0), "disable shortcuts");
    CHECK_OK(kui_window_set_shortcuts_enabled(window, 1), "enable shortcuts");
    CHECK_OK(kui_window_unbind_all_shortcuts(window), "unbind all");
}

static void test_toast_and_screenshot(KuiWindow* window) {
    KuiDocument* doc = NULL;
    kui_window_get_document(window, &doc);
    kui_document_load_html(doc, "<html><body style='background:#123'><p>hi</p></body></html>", 0);
    for (int i = 0; i < 10; ++i) kui_platform_poll_events();

    KuiCToastOptions options;
    memset(&options, 0, sizeof options);
    options.message = "Saved";
    options.level = KUI_C_TOAST_SUCCESS;
    options.duration_ms = 0;
    options.closable = 1;
    uint32_t id = 0;
    CHECK_OK(kui_document_show_toast(doc, &options, &id), "show toast");
    CHECK(id != 0, "toast id");
    int count = 0;
    kui_document_get_toast_count(doc, &count);
    CHECK(count == 1, "one toast");
    options.level = (KuiCToastLevel)9;
    CHECK(kui_document_show_toast(doc, &options, &id) == KUI_ERR_INVALID_ARG, "toast validates level");
    CHECK_OK(kui_document_dismiss_all_toasts(doc), "dismiss all");
    for (int i = 0; i < 200 && count > 0; ++i) {
        kui_platform_wait_events(0.01);
        kui_document_get_toast_count(doc, &count);
    }
    CHECK(count == 0, "toasts gone after dismiss");

    char path[512];
    const char* temp = getenv("TEMP");
    snprintf(path, sizeof path, "%s/kui_c_desktop_shot.png", temp ? temp : ".");
    remove(path);
    int saved = 0;
    CHECK_OK(kui_window_save_screenshot(window, path, 0, &saved), "save screenshot");
    FILE* file = fopen(path, "rb");
    CHECK(saved == 1 && file != NULL, "screenshot written");
    if (file) {
        unsigned char sig[8] = {0};
        CHECK(fread(sig, 1, 8, file) == 8 && sig[1] == 'P' && sig[2] == 'N' && sig[3] == 'G',
              "screenshot is a PNG");
        fclose(file);
    }
    remove(path);
    kui_document_release(doc);
}

/* Section 14: DevTools stop / state. */
static void test_devtools_lifecycle(KuiWindow* window) {
    int running = -1;
    uint16_t port = 1;
    uint32_t clients = 9;
    CHECK_OK(kui_window_get_devtools_state(window, &running, &port, &clients), "devtools state before start");
    CHECK(running == 0 && port == 0 && clients == 0, "not running yet");
    int started = 0;
    CHECK_OK(kui_window_start_devtools(window, 0, &started), "start devtools");
    if (!started) {
        printf("note: DevTools could not start (port in use?); lifecycle checks skipped\n");
        return;
    }
    kui_window_get_devtools_state(window, &running, &port, &clients);
    CHECK(running == 1 && port != 0 && clients == 0, "running on a port");
    char url[256];
    size_t len = 0;
    kui_window_get_devtools_url(window, url, sizeof url, &len);
    CHECK(strstr(url, "token=") != NULL, "the URL carries the access token");
    CHECK_OK(kui_window_stop_devtools(window), "stop devtools");
    kui_window_get_devtools_state(window, &running, &port, &clients);
    CHECK(running == 0 && port == 0, "stopped");
    CHECK_OK(kui_window_start_devtools(window, 0, &started), "start again");
    kui_window_get_devtools_state(window, &running, NULL, NULL);
    CHECK(started == 1 && running == 1, "restarted");
    CHECK_OK(kui_window_stop_devtools(window), "stop again");
}

int main(void) {
    CHECK_OK(kui_platform_init(), "platform init");
    KuiWindowProps props;
    memset(&props, 0, sizeof props);
    props.title = "kui-c-desktop";
    props.width = 320;
    props.height = 240;
    KuiWindow* window = NULL;
    CHECK_OK(kui_window_create(&props, &window), "create window");
    if (window) {
        test_menu(window);
        test_tray(window);
        test_shortcuts(window);
        test_toast_and_screenshot(window);
        test_devtools_lifecycle(window);
        CHECK_OK(kui_window_release(window), "release window with menu/tray state");
    }
    kui_platform_shutdown();
    printf("kui_c desktop: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
