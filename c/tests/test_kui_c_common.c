/* Section 9 (3.61.8) regression tests: window focus / cursor / fullscreen /
 * frame, DOM tree editing and selectors, pointer capture,
 * element_from_point, clock and monitor lookup. C11. Creates one real window.
 * kui_platform_message_box and kui_platform_open_url are interactive / launch
 * external programs, so only their argument checks are exercised. */
#include <kui_c.h>

#include <stdio.h>
#include <string.h>

static int passed = 0;
static int failed = 0;

#define CHECK(condition, message)                                             \
    do {                                                                       \
        if (condition) ++passed;                                               \
        else { ++failed; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, message); } \
    } while (0)

#define CHECK_OK(expression, message) CHECK((expression) == KUI_OK, message)

static KuiElement* by_id(KuiDocument* doc, const char* id) {
    KuiElement* out = NULL;
    kui_document_get_element_by_id(doc, id, 0, &out);
    return out;
}

static int child_ids(KuiElement* parent, char* buf, size_t capacity) {
    /* Writes the child ids joined by ',' into buf. */
    buf[0] = '\0';
    KuiElement* child = NULL;
    if (kui_element_first_child(parent, &child) != KUI_OK) return 0;
    int count = 0;
    while (child) {
        char id[32];
        size_t len = 0;
        kui_element_get_id(child, id, sizeof id, &len);
        if (count) strncat(buf, ",", capacity - strlen(buf) - 1);
        strncat(buf, id, capacity - strlen(buf) - 1);
        ++count;
        KuiElement* next = NULL;
        KuiStatus status = kui_element_next_sibling(child, &next);
        kui_element_release(child);
        child = status == KUI_OK ? next : NULL;
    }
    return count;
}

static void test_dom(KuiDocument* doc) {
    CHECK_OK(kui_document_load_html(doc,
        "<html><body><div id='list' class='box'><p id='a'>A</p><p id='b'>B</p></div>"
        "<span id='free'>free</span></body></html>", 0), "load html");
    KuiElement* list = by_id(doc, "list");
    KuiElement* a = by_id(doc, "a");
    KuiElement* b = by_id(doc, "b");
    KuiElement* freebie = by_id(doc, "free");
    char ids[128];

    /* insert_before: move #free before #b; NULL ref appends; foreign ref fails. */
    CHECK_OK(kui_element_insert_before(list, freebie, b), "insert_before moves child");
    child_ids(list, ids, sizeof ids);
    CHECK(strcmp(ids, "a,free,b") == 0, "insert_before order");
    CHECK_OK(kui_element_insert_before(list, a, NULL), "insert_before NULL appends");
    child_ids(list, ids, sizeof ids);
    CHECK(strcmp(ids, "free,b,a") == 0, "insert_before NULL appends to end");
    KuiElement* body = NULL;
    kui_document_body(doc, &body);
    CHECK(kui_element_insert_before(list, a, body) == KUI_ERR_NOT_FOUND,
          "insert_before rejects a ref that is not a child");
    CHECK(kui_element_insert_before(a, list, NULL) == KUI_ERR_INVALID_ARG,
          "insert_before rejects inserting an ancestor (cycle)");
    CHECK(kui_element_append_child(a, list) == KUI_ERR_INVALID_ARG,
          "append_child rejects appending an ancestor (cycle)");
    CHECK(kui_element_append_child(list, list) == KUI_ERR_INVALID_ARG,
          "append_child rejects appending the element to itself");

    /* replace_child */
    KuiElement* fresh = NULL;
    CHECK_OK(kui_document_create_element(doc, "p", 0, &fresh), "create p");
    kui_element_set_attribute(fresh, "id", 0, "c", 0);
    CHECK_OK(kui_element_replace_child(list, fresh, b), "replace_child");
    child_ids(list, ids, sizeof ids);
    CHECK(strcmp(ids, "free,c,a") == 0, "replace_child result");
    CHECK(kui_element_replace_child(list, fresh, b) == KUI_ERR_NOT_FOUND,
          "replace_child rejects a non-child");

    /* clone, matches, closest, outer html, insert_adjacent_html, append_text */
    KuiElement* copy = NULL;
    CHECK_OK(kui_element_clone(list, 1, &copy), "deep clone");
    size_t count = 0;
    kui_element_child_count(copy, &count);
    CHECK(count == 3, "deep clone copies children");
    KuiElement* parent = NULL;
    CHECK(kui_element_parent(copy, &parent) == KUI_ERR_NOT_FOUND, "clone is detached");
    kui_element_release(copy);
    CHECK_OK(kui_element_clone(list, 0, &copy), "shallow clone");
    kui_element_child_count(copy, &count);
    CHECK(count == 0, "shallow clone has no children");
    kui_element_release(copy);

    int matches = 0;
    CHECK_OK(kui_element_matches(list, "div.box", 0, &matches), "matches");
    CHECK(matches == 1, "matches true");
    CHECK_OK(kui_element_matches(list, "span", 0, &matches), "matches false");
    CHECK(matches == 0, "matches false value");
    KuiElement* up = NULL;
    CHECK_OK(kui_element_closest(a, ".box", 0, &up), "closest");
    char id[32];
    size_t len = 0;
    kui_element_get_id(up, id, sizeof id, &len);
    CHECK(strcmp(id, "list") == 0, "closest finds the ancestor");
    kui_element_release(up);
    CHECK(kui_element_closest(a, ".missing", 0, &up) == KUI_ERR_NOT_FOUND, "closest none");

    char html[256];
    CHECK_OK(kui_element_get_outer_html(a, html, sizeof html, &len), "outer html");
    CHECK(strstr(html, "<p") == html && strstr(html, "id=\"a\"") != NULL, "outer html content");

    char value[64];
    CHECK_OK(kui_element_set_style_property(list, "margin-left", 0, "12px", 0, 0), "set margin");
    CHECK_OK(kui_element_get_computed_style_value(list, "display", 0, value, sizeof value, &len),
             "computed display");
    CHECK(strcmp(value, "block") == 0, "computed display value");
    kui_element_get_computed_style_value(list, "margin-left", 0, value, sizeof value, &len);
    CHECK(strcmp(value, "12px") == 0, "computed margin follows a style change without a frame");
    CHECK_OK(kui_element_get_computed_style_value(list, "no-such-property", 0, value, sizeof value,
                                                  &len), "unknown property");
    CHECK(len == 0 && value[0] == '\0', "unknown property yields empty");
    CHECK(kui_element_get_computed_style_value(list, NULL, 0, value, sizeof value, &len) ==
              KUI_ERR_INVALID_ARG, "name is required");

    CHECK_OK(kui_element_insert_adjacent_html(list, "beforeend", 0, "<i id='tail'>t</i>", 0),
             "insert_adjacent_html beforeend");
    child_ids(list, ids, sizeof ids);
    CHECK(strcmp(ids, "free,c,a,tail") == 0, "insert_adjacent_html result");
    CHECK(kui_element_insert_adjacent_html(list, "inside", 0, "<i></i>", 0) == KUI_ERR_INVALID_ARG,
          "insert_adjacent_html rejects unknown position");
    KuiElement* detached = NULL;
    kui_document_create_element(doc, "div", 0, &detached);
    CHECK(kui_element_insert_adjacent_html(detached, "afterend", 0, "<i></i>", 0) ==
              KUI_ERR_NOT_SUPPORTED,
          "afterend needs a parent");
    kui_element_release(detached);

    CHECK_OK(kui_element_append_text(a, " more", 0), "append_text");
    char text[64];
    kui_element_get_text_content(a, text, sizeof text, &len);
    CHECK(strcmp(text, "A more") == 0, "append_text result");

    /* focus and pointer capture */
    int flag = 1;
    CHECK_OK(kui_element_has_focus(list, &flag), "has_focus");
    CHECK(flag == 0, "not focused");
    CHECK_OK(kui_element_set_pointer_capture(a, 1), "set pointer capture");
    kui_element_has_pointer_capture(a, 1, &flag);
    CHECK(flag == 1, "pointer captured");
    CHECK_OK(kui_element_release_pointer_capture(a, 1), "release pointer capture");
    kui_element_has_pointer_capture(a, 1, &flag);
    CHECK(flag == 0, "pointer released");

    /* getElementsByClassName / TagName (same buffer protocol as query_selector_all) */
    size_t found = 0;
    CHECK(kui_document_get_elements_by_tag_name(doc, "p", 0, NULL, 0, &found) == KUI_OK && found == 2,
          "getElementsByTagName count");
    KuiElement* items[4] = {0};
    CHECK(kui_document_get_elements_by_tag_name(doc, "p", 0, items, 1, &found) == KUI_ERR_BUFFER_TOO_SMALL,
          "getElementsByTagName reports a short buffer");
    CHECK_OK(kui_document_get_elements_by_tag_name(doc, "p", 0, items, 4, &found), "getElementsByTagName");
    for (size_t i = 0; i < found; ++i) kui_element_release(items[i]);
    CHECK_OK(kui_document_get_elements_by_class_name(doc, "box", 0, items, 4, &found),
             "getElementsByClassName");
    CHECK(found == 1, "getElementsByClassName count");
    for (size_t i = 0; i < found; ++i) kui_element_release(items[i]);

    kui_element_release(fresh);
    kui_element_release(body);
    kui_element_release(freebie);
    kui_element_release(b);
    kui_element_release(a);
    kui_element_release(list);
}

static void test_platform(void) {
    double first = 0, second = 0;
    CHECK_OK(kui_platform_now(&first), "now");
    kui_platform_wait_events(0.02);
    kui_platform_now(&second);
    CHECK(second >= first && first > 0, "now is monotonic");
    CHECK(kui_platform_now(NULL) == KUI_ERR_INVALID_ARG, "now validates output");

    KuiCMonitorInfo primary;
    kui_platform_get_primary_monitor(&primary);
    KuiCMonitorInfo hit;
    char name[128];
    size_t len = 0;
    CHECK_OK(kui_platform_get_monitor_from_point(primary.bounds_x + 1, primary.bounds_y + 1, &hit,
                                                 name, sizeof name, &len),
             "monitor from point");
    CHECK(hit.is_primary == 1, "point on the primary monitor");

    CHECK(kui_platform_message_box("t", 0, NULL, 0, KUI_C_MESSAGE_BOX_INFO, KUI_C_MESSAGE_BOX_OK,
                                   NULL) == KUI_ERR_INVALID_ARG,
          "message box validates message");
    CHECK(kui_platform_message_box("t", 0, "m", 0, (KuiCMessageBoxType)9, KUI_C_MESSAGE_BOX_OK,
                                   NULL) == KUI_ERR_INVALID_ARG,
          "message box validates type");
    CHECK(kui_platform_open_url(NULL, 0) == KUI_ERR_INVALID_ARG, "open_url validates url");
}

static void test_window(void) {
    KuiWindowProps props;
    memset(&props, 0, sizeof props);
    props.title = "kui-c-common";
    props.width = 320;
    props.height = 240;
    KuiWindow* window = NULL;
    CHECK_OK(kui_window_create(&props, &window), "create window");
    if (!window) return;

    int value = -1;
    CHECK_OK(kui_window_focus(window), "focus");
    CHECK_OK(kui_window_has_focus(window, &value), "has_focus");
    CHECK_OK(kui_window_set_cursor(window, KUI_C_CURSOR_POINTER), "set cursor");
    CHECK(kui_window_set_cursor(window, (KuiCCursor)99) == KUI_ERR_INVALID_ARG,
          "set cursor validates value");
    CHECK_OK(kui_window_set_cursor_visible(window, 1), "cursor visible");

    CHECK_OK(kui_window_set_fullscreen(window, 1), "fullscreen on");
    kui_window_is_fullscreen(window, &value);
    CHECK(value == 1, "is fullscreen");
    CHECK_OK(kui_window_set_fullscreen(window, 0), "fullscreen off");
    kui_window_is_fullscreen(window, &value);
    CHECK(value == 0, "not fullscreen");

    int frameless = -1, shadow = -1, sizeable = -1;
    CHECK_OK(kui_window_set_frameless(window, 1), "set frameless");
    CHECK_OK(kui_window_set_frameless_shadow(window, 1), "set frameless shadow");
    CHECK_OK(kui_window_get_frame_state(window, &frameless, &shadow, &sizeable), "frame state");
#ifdef _WIN32
    CHECK(frameless == 1 && shadow == 1, "frame state reflects setters");
#else
    /* Tripwire for T-DEBT-POSIX-FRAMELESS-RUNTIME-NOOP: the Linux / macOS core does not
     * implement frameless windows yet (the setters are no-ops). When it does, this fails;
     * switch back to the Windows assertion in the same change. */
    CHECK(frameless == 0 && shadow == 0, "frame state: POSIX core has no frameless yet");
#endif
    CHECK_OK(kui_window_get_frame_state(window, NULL, NULL, NULL), "frame state accepts NULLs");

    float tl = 0, tr = 0, br = 0, bl = 0;
    CHECK_OK(kui_window_set_frame_radii(window, 4, 8, 12, 16), "set radii");
    CHECK_OK(kui_window_get_frame_radii(window, &tl, &tr, &br, &bl), "get radii");
    CHECK(tl == 4 && tr == 8 && br == 12 && bl == 16, "radii round-trip");
    CHECK_OK(kui_window_clear_frame_radius(window), "clear radius");
    CHECK(kui_window_get_frame_radii(window, &tl, &tr, &br, &bl) == KUI_ERR_NOT_FOUND,
          "no radii after clear");
    CHECK_OK(kui_window_set_frameless(window, 0), "frameless off");
    CHECK_OK(kui_window_set_ime_candidate_pos(window, 10, 20), "ime candidate pos");

    KuiCMonitorInfo monitor;
    char name[128];
    size_t len = 0;
    void* native = NULL;
    kui_window_get_native_handle(window, &native);
    CHECK_OK(kui_platform_get_monitor_from_window(native, &monitor, name, sizeof name, &len),
             "monitor from window");
    CHECK(monitor.bounds_width > 0 && monitor.scale > 0, "window monitor info");

    /* element_from_point needs layout: a full-viewport block. */
    KuiDocument* doc = NULL;
    kui_window_get_document(window, &doc);
    kui_document_load_html(doc,
        "<html><body style='margin:0'><div id='hit' style='width:100%;height:200px'></div>"
        "</body></html>", 0);
    for (int i = 0; i < 20; ++i) kui_platform_poll_events();
    KuiElement* hit = NULL;
    CHECK_OK(kui_document_element_from_point(doc, 10, 10, &hit), "element_from_point");
    if (hit) {
        char id[16];
        kui_element_get_id(hit, id, sizeof id, &len);
        CHECK(strcmp(id, "hit") == 0, "element_from_point finds the block");
        kui_element_release(hit);
    }
    kui_document_release(doc);
    kui_window_release(window);
}

int main(void) {
    CHECK_OK(kui_platform_init(), "platform init");
    KuiDocument* doc = NULL;
    CHECK_OK(kui_document_create(&doc), "document create");
    if (doc) {
        test_dom(doc);
        kui_document_release(doc);
    }
    test_platform();
    test_window();
    kui_platform_shutdown();
    printf("kui_c common: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
