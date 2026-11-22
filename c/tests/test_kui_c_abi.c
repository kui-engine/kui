/* =========================================================================
 * test_kui_c_abi.c - C ABI regression tests, written in C on purpose
 *
 * Written in C rather than C++ so that it also proves kui_c.h is C-clean and
 * that the ABI is usable from a translation unit with no C++ runtime at all.
 * ========================================================================= */
#include <kui_c.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compile-time ABI tripwires. A source-compatible edit can still break every
 * foreign-language binding if an enum value or public POD layout moves. */
static_assert(sizeof(KuiVersion) == 12, "KuiVersion ABI size changed");
static_assert(sizeof(KuiRect) == 16, "KuiRect ABI size changed");
static_assert(sizeof(KuiCMonitorInfo) == 40, "KuiCMonitorInfo ABI size changed");
#if UINTPTR_MAX == UINT64_MAX
static_assert(sizeof(KuiWindowProps) == 80, "KuiWindowProps x64 ABI size changed");
#endif
static_assert(KUI_OK == 0, "KuiStatus ABI value changed");
static_assert(KUI_ERR_INVALID_ARG == 1, "KuiStatus ABI value changed");
static_assert(KUI_ERR_BUFFER_TOO_SMALL == 8, "KuiStatus ABI value changed");
static_assert(KUI_C_MOUSE_MOVED == 0, "KuiCMouseEventType ABI value changed");
static_assert(KUI_C_MOUSE_PRESSED == 1, "KuiCMouseEventType ABI value changed");
static_assert(KUI_C_MOUSE_RELEASED == 2, "KuiCMouseEventType ABI value changed");
static_assert(KUI_C_MOUSE_WHEEL == 3, "KuiCMouseEventType ABI value changed");
static_assert(KUI_C_KEY_DOWN == 0, "KuiCKeyEventType ABI value changed");
static_assert(KUI_C_KEY_UP == 1, "KuiCKeyEventType ABI value changed");

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, what)                                                  \
    do {                                                                   \
        if (cond) {                                                        \
            ++g_passed;                                                    \
        } else {                                                           \
            ++g_failed;                                                    \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, (what));      \
        }                                                                  \
    } while (0)

#define CHECK_STATUS(expr, expected, what)                                 \
    do {                                                                   \
        KuiStatus _s = (expr);                                             \
        if (_s == (expected)) {                                            \
            ++g_passed;                                                    \
        } else {                                                           \
            ++g_failed;                                                    \
            printf("  FAIL %s:%d  %s -- expected %s, got %s\n",            \
                   __FILE__, __LINE__, (what),                             \
                   kui_status_message(expected), kui_status_message(_s));  \
        }                                                                  \
    } while (0)

static const char* kHtml =
    "<html><body>"
    "  <h1 id='title' class='greeting big'>Hello</h1>"
    "  <p id='empty'></p>"
    "</body></html>";

/* ------------------------------------------------------------------------- */
static void test_version_and_status(void) {
    KuiVersion v = kui_get_version();
    printf("[version & status]\n");
    CHECK(v.major == KUI_C_API_VERSION_MAJOR, "runtime/header major aligned");
    CHECK(v.minor == KUI_C_API_VERSION_MINOR, "runtime/header minor aligned");
    CHECK(v.patch == KUI_C_API_VERSION_PATCH, "runtime/header patch aligned");
    CHECK(kui_status_message(KUI_OK) != NULL, "status message never NULL");
    CHECK(strcmp(kui_status_message(KUI_ERR_NOT_FOUND), "not found") == 0,
          "NOT_FOUND message");
    /* An out-of-range value must still return a usable string, not crash. */
    CHECK(kui_status_message((KuiStatus)12345) != NULL,
          "unknown status still yields a string");
}

/* ------------------------------------------------------------------------- */
static void test_null_arguments(void) {
    KuiDocument* doc = NULL;
    KuiElement*  el  = NULL;
    size_t       len = 0;
    char         buf[8];

    printf("[null arguments]\n");
    CHECK_STATUS(kui_document_create(NULL), KUI_ERR_INVALID_ARG, "create(NULL)");
    CHECK_STATUS(kui_document_release(NULL), KUI_ERR_INVALID_ARG, "release(NULL)");
    CHECK_STATUS(kui_document_retain(NULL), KUI_ERR_INVALID_ARG, "retain(NULL)");
    CHECK_STATUS(kui_element_release(NULL), KUI_ERR_INVALID_ARG, "el release(NULL)");
    CHECK_STATUS(kui_element_get_tag_name(NULL, buf, sizeof buf, &len),
                 KUI_ERR_INVALID_ARG, "get_tag_name(NULL)");

    CHECK_STATUS(kui_document_create(&doc), KUI_OK, "create");
    CHECK_STATUS(kui_document_body(doc, NULL), KUI_ERR_INVALID_ARG, "body(out=NULL)");
    CHECK_STATUS(kui_document_get_element_by_id(doc, "x", 0, NULL),
                 KUI_ERR_INVALID_ARG, "by_id(out=NULL)");
    CHECK_STATUS(kui_element_has_attribute(NULL, "a", 0, NULL),
                 KUI_ERR_INVALID_ARG, "has_attribute all NULL");
    (void)el;
    kui_document_release(doc);
}

/* ------------------------------------------------------------------------- */
static void test_lookup_and_not_found(void) {
    KuiDocument* doc = NULL;
    KuiElement*  el  = NULL;

    printf("[lookup]\n");
    CHECK_STATUS(kui_document_create(&doc), KUI_OK, "create");
    CHECK_STATUS(kui_document_load_html(doc, kHtml, 0), KUI_OK, "load_html");

    CHECK_STATUS(kui_document_body(doc, &el), KUI_OK, "body");
    CHECK(el != NULL, "body handle non-NULL");
    kui_element_release(el);
    el = NULL;

    CHECK_STATUS(kui_document_get_element_by_id(doc, "title", 0, &el), KUI_OK,
                 "get_element_by_id");
    kui_element_release(el);
    el = NULL;

    /* Absent id must report NOT_FOUND and leave out untouched at NULL. */
    CHECK_STATUS(kui_document_get_element_by_id(doc, "nope", 0, &el),
                 KUI_ERR_NOT_FOUND, "absent id");
    CHECK(el == NULL, "out stays NULL on NOT_FOUND");

    CHECK_STATUS(kui_document_query_selector(doc, ".greeting", 0, &el), KUI_OK,
                 "query_selector by class");
    kui_element_release(el);
    el = NULL;
    CHECK_STATUS(kui_document_query_selector(doc, ".absent", 0, &el),
                 KUI_ERR_NOT_FOUND, "query_selector no match");

    /* Explicit length instead of NUL termination: pass a longer buffer and say
     * how many bytes count. */
    CHECK_STATUS(kui_document_get_element_by_id(doc, "titleXXXX", 5, &el), KUI_OK,
                 "explicit length ignores trailing bytes");
    if (el) { kui_element_release(el); el = NULL; }

    kui_document_release(doc);
}

/* ------------------------------------------------------------------------- */
static void test_two_call_string_pattern(void) {
    KuiDocument* doc = NULL;
    KuiElement*  el  = NULL;
    size_t       need = 0, len = 0;
    /* Must be smaller than strlen(tag) + 1 to trigger truncation. The tag here
     * is "h1", so 3 bytes would fit exactly and correctly return KUI_OK. */
    char         small[2];
    char*        exact;

    printf("[two-call string pattern]\n");
    kui_document_create(&doc);
    kui_document_load_html(doc, kHtml, 0);
    kui_document_get_element_by_id(doc, "title", 0, &el);

    /* 1. length query */
    CHECK_STATUS(kui_element_get_tag_name(el, NULL, 0, &need), KUI_OK,
                 "length query with NULL buffer");
    CHECK(need > 0, "reported length non-zero");

    /* 2. a buffer that is too small: truncated, NUL-terminated, full length
     *    still reported */
    CHECK_STATUS(kui_element_get_tag_name(el, small, sizeof small, &len),
                 KUI_ERR_BUFFER_TOO_SMALL, "too-small buffer");
    CHECK(small[sizeof small - 1] == '\0', "truncated output still NUL-terminated");
    CHECK(len == need, "out_len reports required length, not written length");

    /* 3. exactly-sized buffer (need + 1 for the terminator) */
    exact = (char*)malloc(need + 1);
    CHECK(exact != NULL, "malloc");
    if (exact) {
        CHECK_STATUS(kui_element_get_tag_name(el, exact, need + 1, &len), KUI_OK,
                     "exactly-sized buffer");
        CHECK(strlen(exact) == need, "written length matches reported");
        free(exact);
    }

    /* 4. length query without out_len has no observable effect -> error */
    CHECK_STATUS(kui_element_get_tag_name(el, NULL, 0, NULL), KUI_ERR_INVALID_ARG,
                 "no buffer and no out_len");

    /* 5. an empty value round-trips as length 0 rather than an error */
    {
        KuiElement* empty = NULL;
        char        buf[8];
        kui_document_get_element_by_id(doc, "empty", 0, &empty);
        CHECK_STATUS(kui_element_get_text_content(empty, buf, sizeof buf, &len),
                     KUI_OK, "empty text content");
        CHECK(len == 0 && buf[0] == '\0', "empty value is length 0, NUL-terminated");
        kui_element_release(empty);
    }

    kui_element_release(el);
    kui_document_release(doc);
}

/* ------------------------------------------------------------------------- */
static void test_attributes(void) {
    KuiDocument* doc = NULL;
    KuiElement*  el  = NULL;
    char         buf[64];
    size_t       len = 0;
    int          has = 0;

    printf("[attributes]\n");
    kui_document_create(&doc);
    kui_document_load_html(doc, kHtml, 0);
    kui_document_get_element_by_id(doc, "title", 0, &el);

    CHECK_STATUS(kui_element_get_attribute(el, "class", 0, buf, sizeof buf, &len),
                 KUI_OK, "get existing attribute");
    CHECK(strcmp(buf, "greeting big") == 0, "attribute value");

    /* Absence must be NOT_FOUND, not an empty string -- otherwise a caller
     * cannot tell `class=""` from no class at all. */
    CHECK_STATUS(kui_element_get_attribute(el, "missing", 0, buf, sizeof buf, &len),
                 KUI_ERR_NOT_FOUND, "absent attribute is NOT_FOUND");

    CHECK_STATUS(kui_element_has_attribute(el, "class", 0, &has), KUI_OK, "has_attribute");
    CHECK(has == 1, "has_attribute true");
    CHECK_STATUS(kui_element_has_attribute(el, "missing", 0, &has), KUI_OK, "has_attribute 2");
    CHECK(has == 0, "has_attribute false");

    CHECK_STATUS(kui_element_set_attribute(el, "data-x", 0, "42", 0), KUI_OK, "set");
    CHECK_STATUS(kui_element_get_attribute(el, "data-x", 0, buf, sizeof buf, &len),
                 KUI_OK, "get after set");
    CHECK(strcmp(buf, "42") == 0, "value after set");

    /* Setting an empty value keeps the attribute present. */
    CHECK_STATUS(kui_element_set_attribute(el, "data-x", 0, "", 0), KUI_OK, "set empty");
    CHECK_STATUS(kui_element_has_attribute(el, "data-x", 0, &has), KUI_OK, "has after empty");
    CHECK(has == 1, "empty-valued attribute is still present");

    CHECK_STATUS(kui_element_remove_attribute(el, "data-x", 0), KUI_OK, "remove");
    CHECK_STATUS(kui_element_has_attribute(el, "data-x", 0, &has), KUI_OK, "has after remove");
    CHECK(has == 0, "removed attribute absent");

    kui_element_release(el);
    kui_document_release(doc);
}

/* ------------------------------------------------------------------------- */
/* The reason handles are reference-counted rather than raw pointers: a handle
 * must not dangle when the element leaves the document. Reloading the HTML
 * replaces the entire old tree, which in a raw-pointer design would leave the
 * handle pointing at freed memory. */
static void test_handle_outlives_dom_removal(void) {
    KuiDocument* doc = NULL;
    KuiElement*  el  = NULL;
    char         tag[64];
    size_t       len = 0;

    printf("[handle outlives DOM removal]\n");
    kui_document_create(&doc);
    kui_document_load_html(doc, kHtml, 0);
    kui_document_get_element_by_id(doc, "title", 0, &el);

    CHECK_STATUS(kui_element_get_tag_name(el, tag, sizeof tag, &len), KUI_OK,
                 "tag before reload");
    CHECK(len > 0, "tag non-empty before reload");

    /* Replace the whole tree the element belonged to. */
    CHECK_STATUS(kui_document_load_html(doc, "<html><body><p>new</p></body></html>", 0),
                 KUI_OK, "reload replaces tree");

    /* The element is no longer reachable from the document... */
    {
        KuiElement* gone = NULL;
        CHECK_STATUS(kui_document_get_element_by_id(doc, "title", 0, &gone),
                     KUI_ERR_NOT_FOUND, "old id no longer in document");
    }

    /* ...but the handle we hold is still valid, because it owns a reference. */
    memset(tag, 0, sizeof tag);
    CHECK_STATUS(kui_element_get_tag_name(el, tag, sizeof tag, &len), KUI_OK,
                 "handle still usable after its element left the tree");
    CHECK(len > 0, "tag still readable after removal");

    kui_element_release(el);
    kui_document_release(doc);
}

/* ------------------------------------------------------------------------- */
/* retain/release must balance: an extra retain keeps the object alive past the
 * first release. Verified by using the handle in between. */
static void test_retain_release_balance(void) {
    KuiDocument* doc = NULL;
    KuiElement*  el  = NULL;
    char         tag[64];
    size_t       len = 0;

    printf("[retain/release balance]\n");
    kui_document_create(&doc);
    kui_document_load_html(doc, kHtml, 0);
    kui_document_get_element_by_id(doc, "title", 0, &el);

    CHECK_STATUS(kui_element_retain(el), KUI_OK, "retain");
    CHECK_STATUS(kui_element_release(el), KUI_OK, "first release");
    /* Still alive thanks to the retain. */
    CHECK_STATUS(kui_element_get_tag_name(el, tag, sizeof tag, &len), KUI_OK,
                 "usable after balanced release");
    CHECK_STATUS(kui_element_release(el), KUI_OK, "final release");

    /* Same for the document handle. */
    CHECK_STATUS(kui_document_retain(doc), KUI_OK, "doc retain");
    CHECK_STATUS(kui_document_release(doc), KUI_OK, "doc release 1");
    CHECK_STATUS(kui_document_body(doc, &el), KUI_OK, "doc usable after balanced release");
    if (el) kui_element_release(el);
    CHECK_STATUS(kui_document_release(doc), KUI_OK, "doc final release");
}

/* ------------------------------------------------------------------------- */
static void test_create_element_and_text(void) {
    KuiDocument* doc = NULL;
    KuiElement*  el  = NULL;
    char         buf[64];
    size_t       len = 0;

    printf("[create element & text]\n");
    kui_document_create(&doc);
    kui_document_load_html(doc, kHtml, 0);

    CHECK_STATUS(kui_document_create_element(doc, "div", 0, &el), KUI_OK, "create_element");
    CHECK(el != NULL, "new element handle");

    CHECK_STATUS(kui_element_set_text_content(el, "text via C", 0), KUI_OK, "set text");
    CHECK_STATUS(kui_element_get_text_content(el, buf, sizeof buf, &len), KUI_OK, "get text");
    CHECK(strcmp(buf, "text via C") == 0, "text round-trip");

    /* UTF-8 must survive unchanged. */
    CHECK_STATUS(kui_element_set_text_content(el, "\xE4\xB8\xAD\xE6\x96\x87", 0), KUI_OK,
                 "set UTF-8 text");
    CHECK_STATUS(kui_element_get_text_content(el, buf, sizeof buf, &len), KUI_OK,
                 "get UTF-8 text");
    CHECK(len == 6, "UTF-8 length is in bytes");
    CHECK(strcmp(buf, "\xE4\xB8\xAD\xE6\x96\x87") == 0, "UTF-8 bytes unchanged");

    kui_element_release(el);
    kui_document_release(doc);
}

/* ------------------------------------------------------------------------- */
static void test_extended_dom(void) {
    KuiDocument* doc = NULL;
    KuiElement* body = NULL;
    KuiElement* parent = NULL;
    KuiElement* child = NULL;
    KuiElement* found = NULL;
    KuiElement* results[2] = {NULL, NULL};
    char buf[128];
    size_t len = 0;
    size_t count = 0;
    int contains = 0;

    printf("[extended DOM]\n");
    CHECK_STATUS(kui_document_create(&doc), KUI_OK, "create");
    CHECK_STATUS(kui_document_load_html(doc, kHtml, 0), KUI_OK, "load");
    CHECK_STATUS(kui_document_add_stylesheet(doc, ".item { color: red; }", 0),
                 KUI_OK, "add stylesheet");
    CHECK_STATUS(kui_document_clear_stylesheets(doc), KUI_OK, "clear stylesheets");
    CHECK_STATUS(kui_document_body(doc, &body), KUI_OK, "body");
    CHECK_STATUS(kui_document_create_element(doc, "section", 0, &parent), KUI_OK,
                 "create parent");
    CHECK_STATUS(kui_document_create_element(doc, "span", 0, &child), KUI_OK,
                 "create child");
    CHECK_STATUS(kui_element_set_attribute(parent, "id", 0, "host", 0), KUI_OK,
                 "parent id");
    CHECK_STATUS(kui_element_set_class_name(child, "item hot", 0), KUI_OK,
                 "set className");
    CHECK_STATUS(kui_element_get_class_name(child, buf, sizeof buf, &len), KUI_OK,
                 "get className");
    CHECK(strcmp(buf, "item hot") == 0, "className round-trip");
    CHECK_STATUS(kui_element_class_list_toggle(child, "hot", 0), KUI_OK,
                 "toggle class off");
    CHECK_STATUS(kui_element_class_list_contains(child, "hot", 0, &contains), KUI_OK,
                 "contains toggled class");
    CHECK(contains == 0, "toggle removed class");

    CHECK_STATUS(kui_element_append_child(parent, child), KUI_OK, "append child");
    CHECK_STATUS(kui_element_append_child(body, parent), KUI_OK, "append parent");
    CHECK_STATUS(kui_element_child_count(parent, &count), KUI_OK, "child count");
    CHECK(count == 1, "one element child");
    CHECK_STATUS(kui_element_last_child(parent, &found), KUI_OK, "last child");
    CHECK(found != NULL, "last child handle");
    if (found) { kui_element_release(found); found = NULL; }

    CHECK_STATUS(kui_element_set_inner_html(parent,
                 "<b class='item'>one</b><i class='item'>two</i>", 0),
                 KUI_OK, "set innerHTML");
    CHECK_STATUS(kui_element_get_inner_html(parent, buf, sizeof buf, &len), KUI_OK,
                 "get innerHTML");
    CHECK(strstr(buf, "one") != NULL && strstr(buf, "two") != NULL,
          "innerHTML serialized content");
    CHECK_STATUS(kui_element_query_selector(parent, "b.item", 0, &found), KUI_OK,
                 "element querySelector");
    if (found) { kui_element_release(found); found = NULL; }
    CHECK_STATUS(kui_document_query_selector_all(doc, ".item", 0, NULL, 0, &count),
                 KUI_OK, "querySelectorAll count");
    CHECK(count == 2, "querySelectorAll count is two");
    CHECK_STATUS(kui_document_query_selector_all(doc, ".item", 0, results, 2, &count),
                 KUI_OK, "querySelectorAll values");
    if (results[0]) kui_element_release(results[0]);
    if (results[1]) kui_element_release(results[1]);

    CHECK_STATUS(kui_element_remove_child(body, parent), KUI_OK, "remove child");
    CHECK_STATUS(kui_element_get_tag_name(parent, buf, sizeof buf, &len), KUI_OK,
                 "removed handle remains valid");

    kui_element_release(child);
    kui_element_release(parent);
    kui_element_release(body);
    kui_document_release(doc);
}

/* ------------------------------------------------------------------------- */
/* Events. The callback is a plain C function receiving user_data, which is how
 * state gets out of it -- there is no capture mechanism in C. */
typedef struct {
    int    calls;
    char   last_type[32];
    int    target_was_element;
    int    mouse_probe_status;   /* what a mouse accessor returned on this event */
    int    prevent;              /* set by the test: call preventDefault? */
    int    stop;                 /* set by the test: call stopPropagation? */
} ListenerState;

static void on_event(KuiEvent* evt, void* user_data) {
    ListenerState* st = (ListenerState*)user_data;
    KuiElement*    target = NULL;
    size_t         len = 0;
    float          x = 0.0f, y = 0.0f;

    ++st->calls;
    kui_event_get_type(evt, st->last_type, sizeof st->last_type, &len);

    /* A handle obtained from an event is an ordinary counted handle and must be
     * released -- unlike the KuiEvent* itself. */
    if (kui_event_get_target(evt, &target) == KUI_OK && target) {
        st->target_was_element = 1;
        kui_element_release(target);
    }

    /* This is a plain Event, not a MouseEvent, so mouse accessors must decline
     * rather than return garbage. */
    st->mouse_probe_status = (int)kui_event_mouse_get_client_xy(evt, &x, &y);

    if (st->prevent) kui_event_prevent_default(evt);
    if (st->stop)    kui_event_stop_propagation(evt);
}

static void test_events(void) {
    KuiDocument*  doc = NULL;
    KuiElement*   title = NULL;
    KuiElement*   body = NULL;
    ListenerState st;
    ListenerState parent_st;
    KuiListenerId id = 0, parent_id = 0;
    int           not_canceled = -1;

    printf("[events]\n");
    kui_document_create(&doc);
    kui_document_load_html(doc, kHtml, 0);
    kui_document_get_element_by_id(doc, "title", 0, &title);
    kui_document_body(doc, &body);

    memset(&st, 0, sizeof st);
    CHECK_STATUS(kui_element_add_event_listener(title, "click", 0, on_event, &st, &id),
                 KUI_OK, "add_event_listener");

    /* Argument validation. */
    CHECK_STATUS(kui_element_add_event_listener(title, "click", 0, NULL, NULL, NULL),
                 KUI_ERR_INVALID_ARG, "add with NULL callback");
    CHECK_STATUS(kui_element_add_event_listener(NULL, "click", 0, on_event, NULL, NULL),
                 KUI_ERR_INVALID_ARG, "add on NULL element");

    /* Dispatch and confirm the trampoline actually reached the C function. */
    CHECK_STATUS(kui_element_dispatch_event(title, "click", 0, 1, 1, &not_canceled),
                 KUI_OK, "dispatch_event");
    CHECK(st.calls == 1, "listener invoked exactly once");
    CHECK(strcmp(st.last_type, "click") == 0, "event type readable in callback");
    CHECK(st.target_was_element == 1, "event target resolved to an element");
    CHECK(st.mouse_probe_status == (int)KUI_ERR_NOT_SUPPORTED,
          "mouse accessor declines a non-mouse event");
    CHECK(not_canceled == 1, "not canceled when nobody prevents default");

    /* preventDefault must come back through the dispatch return. */
    st.prevent = 1;
    not_canceled = -1;
    CHECK_STATUS(kui_element_dispatch_event(title, "click", 0, 1, 1, &not_canceled),
                 KUI_OK, "dispatch with preventDefault");
    CHECK(st.calls == 2, "listener invoked again");
    CHECK(not_canceled == 0, "preventDefault reported as canceled");
    st.prevent = 0;

    /* Bubbling: a listener on body sees an event dispatched at title. */
    memset(&parent_st, 0, sizeof parent_st);
    CHECK_STATUS(kui_element_add_event_listener(body, "click", 0, on_event, &parent_st,
                                                &parent_id), KUI_OK, "listener on body");
    CHECK_STATUS(kui_element_dispatch_event(title, "click", 0, 1, 1, NULL), KUI_OK,
                 "dispatch bubbling");
    CHECK(parent_st.calls == 1, "event bubbled to body");

    /* stopPropagation on the child keeps it from reaching body. */
    st.stop = 1;
    CHECK_STATUS(kui_element_dispatch_event(title, "click", 0, 1, 1, NULL), KUI_OK,
                 "dispatch with stopPropagation");
    CHECK(parent_st.calls == 1, "stopPropagation kept the event from body");
    st.stop = 0;

    /* A non-bubbling event must not reach body either. */
    CHECK_STATUS(kui_element_dispatch_event(title, "click", 0, 0, 1, NULL), KUI_OK,
                 "dispatch non-bubbling");
    CHECK(parent_st.calls == 1, "non-bubbling event stayed at the target");

    /* Removal needs the event name as well as the id. */
    CHECK_STATUS(kui_element_remove_event_listener(title, "click", 0, id), KUI_OK,
                 "remove_event_listener");
    {
        const int before = st.calls;
        CHECK_STATUS(kui_element_dispatch_event(title, "click", 0, 1, 1, NULL), KUI_OK,
                     "dispatch after removal");
        CHECK(st.calls == before, "removed listener no longer fires");
    }

    /* Bulk removal. */
    CHECK_STATUS(kui_element_remove_event_listeners(body, "click", 0), KUI_OK,
                 "remove_event_listeners");
    {
        const int before = parent_st.calls;
        kui_element_dispatch_event(title, "click", 0, 1, 1, NULL);
        CHECK(parent_st.calls == before, "bulk-removed listeners no longer fire");
    }

    /* A detached element has no document to dispatch through. */
    {
        KuiElement* orphan = NULL;
        kui_document_create_element(doc, "div", 0, &orphan);
        /* create_element attaches an owner document, so dispatch should work;
         * what must not happen is a crash either way. */
        CHECK(kui_element_dispatch_event(orphan, "click", 0, 1, 1, NULL) != KUI_ERR_INTERNAL,
              "dispatch on a detached element does not fault");
        kui_element_release(orphan);
    }

    kui_element_release(body);
    kui_element_release(title);
    kui_document_release(doc);
}

/* ------------------------------------------------------------------------- */
static void test_inline_style(void) {
    KuiDocument* doc = NULL;
    KuiElement*  el  = NULL;
    char         buf[128];
    size_t       len = 0, count = 0;
    int          important = -1;

    printf("[inline style]\n");
    kui_document_create(&doc);
    kui_document_load_html(doc, kHtml, 0);
    kui_document_get_element_by_id(doc, "title", 0, &el);

    /* A property that was never set is absent, not empty. */
    CHECK_STATUS(kui_element_get_style_property(el, "color", 0, buf, sizeof buf, &len),
                 KUI_ERR_NOT_FOUND, "unset property is NOT_FOUND");

    CHECK_STATUS(kui_element_set_style_property(el, "color", 0, "red", 0, 0), KUI_OK,
                 "set color");
    CHECK_STATUS(kui_element_get_style_property(el, "color", 0, buf, sizeof buf, &len),
                 KUI_OK, "get color");
    /* Values come back serialized. Report the actual form so the assertion below
     * is based on measurement rather than assumption. */
    /* Measured: named colours serialize to hex, so the value that comes back is
     * not the string that went in. Asserting the exact form rather than merely
     * "non-empty" so a change in serialization shows up here. */
    CHECK(strcmp(buf, "#ff0000") == 0, "color serializes to #ff0000, not \"red\"");

    CHECK_STATUS(kui_element_set_style_property(el, "width", 0, "100px", 0, 0), KUI_OK,
                 "set width");
    CHECK_STATUS(kui_element_get_style_property(el, "width", 0, buf, sizeof buf, &len),
                 KUI_OK, "get width");
    CHECK(strstr(buf, "100") != NULL, "width round-trips");

    /* !important is tracked separately from the value. */
    CHECK_STATUS(kui_element_style_property_is_important(el, "color", 0, &important),
                 KUI_OK, "is_important on a normal property");
    CHECK(important == 0, "not important by default");
    CHECK_STATUS(kui_element_set_style_property(el, "color", 0, "blue", 0, 1), KUI_OK,
                 "set important");
    CHECK_STATUS(kui_element_style_property_is_important(el, "color", 0, &important),
                 KUI_OK, "is_important after");
    CHECK(important == 1, "important flag observed");
    CHECK_STATUS(kui_element_style_property_is_important(el, "nope", 0, &important),
                 KUI_ERR_NOT_FOUND, "is_important on unset property");

    /* Enumeration, for inspection tools. */
    CHECK_STATUS(kui_element_get_style_property_count(el, &count), KUI_OK, "property count");
    CHECK(count >= 2, "at least the two properties we set");
    CHECK_STATUS(kui_element_get_style_property_name(el, 0, buf, sizeof buf, &len), KUI_OK,
                 "property name at 0");
    CHECK(len > 0, "property name non-empty");
    CHECK_STATUS(kui_element_get_style_property_name(el, count + 100, buf, sizeof buf, &len),
                 KUI_ERR_NOT_FOUND, "out-of-range index");

    /* Whole-declaration text. */
    CHECK_STATUS(kui_element_get_style_text(el, buf, sizeof buf, &len), KUI_OK, "cssText read");
    CHECK(len > 0, "cssText non-empty");
    CHECK_STATUS(kui_element_set_style_text(el, "margin: 4px; padding: 2px", 0), KUI_OK,
                 "cssText write");
    CHECK_STATUS(kui_element_get_style_property(el, "margin", 0, buf, sizeof buf, &len),
                 KUI_OK, "property visible after cssText write");
    /* The old properties are gone, since setCssText replaces the declaration. */
    CHECK_STATUS(kui_element_get_style_property(el, "color", 0, buf, sizeof buf, &len),
                 KUI_ERR_NOT_FOUND, "cssText write replaced the declaration");

    CHECK_STATUS(kui_element_remove_style_property(el, "margin", 0), KUI_OK, "remove");
    CHECK_STATUS(kui_element_get_style_property(el, "margin", 0, buf, sizeof buf, &len),
                 KUI_ERR_NOT_FOUND, "removed property absent");

    CHECK_STATUS(kui_element_set_style_property(NULL, "color", 0, "red", 0, 0),
                 KUI_ERR_INVALID_ARG, "set on NULL element");
    CHECK_STATUS(kui_element_get_style_property_count(el, NULL), KUI_ERR_INVALID_ARG,
                 "count with NULL out");

    kui_element_release(el);
    kui_document_release(doc);
}

/* ------------------------------------------------------------------------- */
/* Geometry reads laid-out boxes. This document was never attached to a window
 * and never laid out, so the values are legitimately zero -- the assertions here
 * are about the calls behaving correctly (writing every requested field,
 * rejecting an all-NULL call), not about specific numbers, which would need a
 * real layout pass to be meaningful. */
static void test_geometry(void) {
    KuiDocument* doc = NULL;
    KuiElement*  el  = NULL;
    KuiRect      r;
    float        a = -1.0f, b = -1.0f, c = -1.0f, d = -1.0f;

    printf("[geometry]\n");
    kui_document_create(&doc);
    kui_document_load_html(doc, kHtml, 0);
    kui_document_get_element_by_id(doc, "title", 0, &el);

    memset(&r, 0xFF, sizeof r);
    CHECK_STATUS(kui_element_get_bounding_rect(el, &r), KUI_OK, "bounding rect");
    CHECK(r.width >= 0.0f && r.height >= 0.0f, "rect dimensions not negative");
    CHECK_STATUS(kui_element_get_bounding_rect(el, NULL), KUI_ERR_INVALID_ARG,
                 "bounding rect with NULL out");

    CHECK_STATUS(kui_element_get_client_metrics(el, &a, &b, &c, &d), KUI_OK, "client metrics");
    CHECK(a != -1.0f || b != -1.0f || c != -1.0f || d != -1.0f, "client metrics written");

    a = b = c = d = -1.0f;
    CHECK_STATUS(kui_element_get_offset_metrics(el, &a, &b, &c, &d), KUI_OK, "offset metrics");
    CHECK(a != -1.0f || b != -1.0f || c != -1.0f || d != -1.0f, "offset metrics written");

    a = b = c = d = -1.0f;
    CHECK_STATUS(kui_element_get_scroll_metrics(el, &a, &b, &c, &d), KUI_OK, "scroll metrics");
    CHECK(a != -1.0f || b != -1.0f || c != -1.0f || d != -1.0f, "scroll metrics written");

    /* Partial reads: ask for one field only. */
    a = -1.0f;
    CHECK_STATUS(kui_element_get_client_metrics(el, NULL, NULL, &a, NULL), KUI_OK,
                 "single-field read");
    CHECK(a != -1.0f, "requested field written");

    /* Asking for nothing is a caller error, not a silent success. */
    CHECK_STATUS(kui_element_get_client_metrics(el, NULL, NULL, NULL, NULL),
                 KUI_ERR_INVALID_ARG, "all-NULL metrics call");
    CHECK_STATUS(kui_element_get_scroll_metrics(NULL, &a, &b, &c, &d),
                 KUI_ERR_INVALID_ARG, "metrics on NULL element");

    kui_element_release(el);
    kui_document_release(doc);
}

/* ------------------------------------------------------------------------- */
static void test_devtools_automation(void) {
    KuiDocument* doc = NULL;
    float quads[32];
    float width = -1.0f, height = -1.0f;
    size_t need = 0;
    char* response = NULL;

    printf("[devtools automation]\n");
    kui_document_create(&doc);
    kui_document_load_html(doc, kHtml, 0);

    CHECK_STATUS(kui_devtools_get_box_model(doc, "#title", quads, &width, &height),
                 KUI_OK, "get box model");
    CHECK(width >= 0.0f && height >= 0.0f, "box model dimensions are non-negative");
    CHECK_STATUS(kui_devtools_get_box_model(doc, ".absent", quads, &width, &height),
                 KUI_ERR_NOT_FOUND, "box model absent selector");
    CHECK_STATUS(kui_devtools_dispatch_mouse_event(
                     doc, (KuiCMouseEventType)99, 0, 0, KUI_C_MOUSE_BUTTON_NONE,
                     0, 1, 0, 0),
                 KUI_ERR_INVALID_ARG, "reject invalid mouse event type");
    CHECK_STATUS(kui_devtools_dispatch_key_event(
                     doc, KUI_C_KEY_DOWN, "a", 0, "KeyA", 0, 16, NULL, 0, 0),
                 KUI_ERR_INVALID_ARG, "reject invalid modifier bits");
    CHECK_STATUS(kui_devtools_dispatch_key_event(
                     doc, KUI_C_KEY_DOWN, "a", 0, "KeyA", 0, 0, NULL, 0, 0),
                 KUI_OK, "dispatch key event");

    CHECK_STATUS(kui_devtools_dispatch_command(
                     doc, "{\"id\":1,\"method\":\"DOM.getDocument\"}",
                     NULL, 0, &need),
                 KUI_OK, "raw command length query");
    CHECK(need > 0, "raw command produced a response");
    response = (char*)malloc(need + 1);
    CHECK(response != NULL, "allocate raw command response");
    if (response) {
        CHECK_STATUS(kui_devtools_dispatch_command(
                         doc, "{\"id\":1,\"method\":\"DOM.getDocument\"}",
                         response, need + 1, &need),
                     KUI_OK, "raw command response");
        CHECK(strstr(response, "\"id\":1") != NULL, "raw command preserves id");
        free(response);
    }

    kui_document_release(doc);
}

/* ------------------------------------------------------------------------- */
/* Platform and Window.
 *
 * What is deliberately NOT tested here: kui_platform_run (blocks until quit),
 * and whether a created window actually appears. Both need a real display and a
 * human or a GUI harness; asserting on them from a headless unit test would be
 * asserting on the environment, not the ABI. examples/window_demo.c covers the
 * full open-a-window path for manual runs.
 *
 * What IS tested: argument validation, which needs no display at all, plus timer
 * registration and clearing when platform init succeeds. */
static int g_timer_fired = 0;

static void on_timer(void* user_data) {
    int* counter = (int*)user_data;
    ++(*counter);
}

static void test_platform_and_window(void) {
    KuiTimerId id = 0;
    int        code = -1;

    printf("[platform & window]\n");

    /* Argument validation happens before anything touches the platform, so these
     * are safe with no init. */
    CHECK_STATUS(kui_platform_set_timeout(NULL, NULL, 10, &id), KUI_ERR_INVALID_ARG,
                 "set_timeout with NULL callback");
    CHECK_STATUS(kui_platform_set_interval(NULL, NULL, 10, &id), KUI_ERR_INVALID_ARG,
                 "set_interval with NULL callback");
    CHECK_STATUS(kui_platform_request_idle(NULL, NULL), KUI_ERR_INVALID_ARG,
                 "request_idle with NULL callback");
    CHECK_STATUS(kui_window_create(NULL, NULL), KUI_ERR_INVALID_ARG,
                 "window_create with NULL out");
    CHECK_STATUS(kui_window_release(NULL), KUI_ERR_INVALID_ARG, "window_release(NULL)");
    CHECK_STATUS(kui_window_show(NULL), KUI_ERR_INVALID_ARG, "window_show(NULL)");
    CHECK_STATUS(kui_window_get_document(NULL, NULL), KUI_ERR_INVALID_ARG,
                 "get_document(NULL)");
    CHECK_STATUS(kui_window_is_visible(NULL, NULL), KUI_ERR_INVALID_ARG,
                 "is_visible(NULL)");
    CHECK_STATUS(kui_window_get_native_handle(NULL, NULL), KUI_ERR_INVALID_ARG,
                 "native_handle(NULL)");
    CHECK_STATUS(kui_window_resize(NULL, 100, 100), KUI_ERR_INVALID_ARG, "resize(NULL)");
    CHECK_STATUS(kui_window_set_opacity(NULL, 0.5f), KUI_ERR_INVALID_ARG, "opacity(NULL)");

    /* Range checks that do not need a window either. */
    {
        KuiWindow* dummy = NULL;
        /* Using NULL for the handle means the NULL check fires first, so these
         * only prove the argument order; the real range check is exercised via
         * the window created below when init succeeds. */
        CHECK_STATUS(kui_window_resize(dummy, -1, 10), KUI_ERR_INVALID_ARG,
                     "negative size rejected");
    }

    /* Everything past here needs a platform. On a machine with no display this
     * legitimately fails, so report and skip rather than fail the suite. */
    if (kui_platform_init() != KUI_OK) {
        printf("    (platform init unavailable here -- skipping timer/window checks)\n");
        return;
    }

    g_timer_fired = 0;
    CHECK_STATUS(kui_platform_set_timeout(on_timer, &g_timer_fired, 50, &id), KUI_OK,
                 "set_timeout registers");
    CHECK(id != 0, "timer id is non-zero");
    CHECK_STATUS(kui_platform_clear_timer(id), KUI_OK, "clear_timer");
    /* Clearing an id twice, or an unknown id, must not fault. */
    CHECK_STATUS(kui_platform_clear_timer(id), KUI_OK, "clear_timer is idempotent");
    CHECK_STATUS(kui_platform_clear_timer(999999), KUI_OK, "clear unknown timer");

    CHECK_STATUS(kui_platform_set_interval(on_timer, &g_timer_fired, 1000, &id), KUI_OK,
                 "set_interval registers");
    CHECK_STATUS(kui_platform_clear_timer(id), KUI_OK, "clear interval");

    /* out_id is optional. */
    CHECK_STATUS(kui_platform_set_timeout(on_timer, &g_timer_fired, 10, NULL), KUI_OK,
                 "set_timeout without out_id");
    CHECK_STATUS(kui_platform_request_idle(on_timer, &g_timer_fired), KUI_OK,
                 "request_idle registers");

    /* Pumping the loop once must return rather than block. */
    CHECK_STATUS(kui_platform_poll_events(), KUI_OK, "poll_events returns");

    /* quit() before run() just records the exit code; it must not fault. */
    CHECK_STATUS(kui_platform_quit(0), KUI_OK, "quit before run");
    (void)code;

    /* A window, created hidden -- no show(), so nothing appears on screen. This
     * still exercises creation, the document handle, and destruction. */
    {
        KuiWindowProps props;
        KuiWindow*     w = NULL;
        KuiDocument*   doc = NULL;
        KuiElement*    body = NULL;
        char           buf[64];
        size_t         len = 0;
        int            visible = -1;
        float          ratio = 0.0f;

        /* memset to zero must yield an ordinary window -- that is the contract
         * the negatively-phrased no_resize / no_decorations fields exist for. */
        memset(&props, 0, sizeof props);
        props.title  = "kui_c test window";
        props.width  = 320;
        props.height = 240;

        if (kui_window_create(&props, &w) != KUI_OK || !w) {
            printf("    (window creation unavailable here -- skipping window checks)\n");
            kui_platform_shutdown();
            return;
        }
        ++g_passed;   /* creation itself */

        CHECK_STATUS(kui_window_get_title(w, buf, sizeof buf, &len), KUI_OK, "get_title");
        CHECK(strcmp(buf, "kui_c test window") == 0, "title round-trips");
        CHECK_STATUS(kui_window_set_title(w, "renamed", 0), KUI_OK, "set_title");
        CHECK_STATUS(kui_window_get_title(w, buf, sizeof buf, &len), KUI_OK, "get_title again");
        CHECK(strcmp(buf, "renamed") == 0, "title updated");

        /* Measured: a freshly created window already reports visible -- creation
         * shows it, there is no hidden-until-show() step. Assert a legal boolean
         * rather than a guessed initial state, then verify hide() actually takes
         * effect, which is the behaviour worth pinning. hide() is used rather
         * than show() so the suite does not flash a window on screen. */
        CHECK_STATUS(kui_window_is_visible(w, &visible), KUI_OK, "is_visible");
        CHECK(visible == 0 || visible == 1, "is_visible reports a boolean");
        CHECK_STATUS(kui_window_hide(w), KUI_OK, "hide");
        CHECK_STATUS(kui_window_is_visible(w, &visible), KUI_OK, "is_visible after hide");
        CHECK(visible == 0, "hidden after hide()");

        CHECK_STATUS(kui_window_get_device_pixel_ratio(w, &ratio), KUI_OK, "dpr");
        CHECK(ratio > 0.0f, "dpr positive");

        CHECK_STATUS(kui_window_set_opacity(w, 1.5f), KUI_ERR_INVALID_ARG,
                     "opacity out of range rejected");
        CHECK_STATUS(kui_window_set_opacity(w, 0.9f), KUI_OK, "opacity in range");
        CHECK_STATUS(kui_window_resize(w, -5, 10), KUI_ERR_INVALID_ARG,
                     "negative resize rejected");

        /* The window owns its document; the handle we get is counted. */
        CHECK_STATUS(kui_window_get_document(w, &doc), KUI_OK, "get_document");
        CHECK(doc != NULL, "document handle non-NULL");
        CHECK_STATUS(kui_document_load_html(doc, kHtml, 0), KUI_OK, "load into window doc");
        CHECK_STATUS(kui_document_body(doc, &body), KUI_OK, "body of window doc");

        {
            void* native = NULL;
            CHECK_STATUS(kui_window_get_native_handle(w, &native), KUI_OK, "native handle");
            CHECK(native != NULL, "native handle non-NULL");
        }

        /* Releasing the window destroys it. The document reference we hold keeps
         * the document alive regardless -- the point of the counted handle. */
        CHECK_STATUS(kui_window_release(w), KUI_OK, "window_release destroys");
        CHECK_STATUS(kui_element_get_tag_name(body, buf, sizeof buf, &len), KUI_OK,
                     "element from a released window's document still usable");

        kui_element_release(body);
        kui_document_release(doc);
    }

    kui_platform_shutdown();
}

/* ------------------------------------------------------------------------- */
int main(void) {
    /* Unbuffered: if a test faults, buffered output is lost and the run looks
     * like it produced nothing at all, which misdirects the diagnosis. */
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("KUI C ABI tests\n\n");

    test_version_and_status();
    test_null_arguments();
    test_lookup_and_not_found();
    test_two_call_string_pattern();
    test_attributes();
    test_handle_outlives_dom_removal();
    test_retain_release_balance();
    test_create_element_and_text();
    test_extended_dom();
    test_events();
    test_inline_style();
    test_geometry();
    test_devtools_automation();
    test_platform_and_window();

    printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
