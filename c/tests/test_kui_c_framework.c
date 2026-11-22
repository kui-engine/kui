/* Section 11 (3.61.8) regression tests: router, data binding, animation state
 * and Mutation / Resize / Intersection observers. C11. Creates one real window
 * (the router view and the resize / intersection checks need rendered frames). */
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

static void pump(int rounds) {
    for (int i = 0; i < rounds; ++i) kui_platform_wait_events(0.01);
}

static int releases = 0;
static void count_release(void* user_data) {
    (void)user_data;
    ++releases;
}

static int text_is(KuiElement* el, const char* expected) {
    char buf[256];
    size_t len = 0;
    if (kui_element_get_text_content(el, buf, sizeof buf, &len) != KUI_OK) return 0;
    return strcmp(buf, expected) == 0;
}

/* -- Router ----------------------------------------------------------------- */
static int views_built = 0;
static KuiElement* build_view(KuiDocument* doc, const KuiCRouteInfo* route, void* user_data) {
    (void)user_data;
    ++views_built;
    KuiElement* el = NULL;
    if (kui_document_create_element(doc, "p", 0, &el) != KUI_OK) return NULL;
    char text[128];
    snprintf(text, sizeof text, "view:%.*s", (int)route->pattern_len, route->pattern);
    for (size_t i = 0; i < route->param_count; ++i) {
        size_t used = strlen(text);
        snprintf(text + used, sizeof text - used, " %.*s=%.*s", (int)route->params[i].key_len,
                 route->params[i].key, (int)route->params[i].value_len, route->params[i].value);
    }
    kui_element_set_attribute(el, "id", 0, "routed", 0);
    kui_element_set_text_content(el, text, 0);
    return el; /* ownership handed to KUI */
}

static int block_admin = 1;
static int guard_calls = 0;
static int guard_enter(const KuiCRouteInfo* from, const KuiCRouteInfo* to, void* user_data) {
    (void)from;
    (void)user_data;
    ++guard_calls;
    return !(block_admin && to->path_len >= 6 && memcmp(to->path, "/admin", 6) == 0);
}

static int navigations = 0;
static char last_query_tab[16];
static void on_navigated(const KuiCRouteInfo* route, void* user_data) {
    (void)user_data;
    ++navigations;
    last_query_tab[0] = '\0';
    for (size_t i = 0; i < route->query_param_count; ++i) {
        if (route->query_params[i].key_len == 3 && memcmp(route->query_params[i].key, "tab", 3) == 0) {
            snprintf(last_query_tab, sizeof last_query_tab, "%.*s",
                     (int)route->query_params[i].value_len, route->query_params[i].value);
        }
    }
}

static char current_pattern[64];
static void read_current(const KuiCRouteInfo* route, void* user_data) {
    (void)user_data;
    snprintf(current_pattern, sizeof current_pattern, "%.*s", (int)route->pattern_len, route->pattern);
}

static void test_router(KuiDocument* doc) {
    kui_document_load_html(doc, "<html><body><main id='view'></main>"
                                "<kui-router-view id='auto'></kui-router-view></body></html>", 0);
    pump(5);
    KuiElement* view = NULL;
    kui_document_get_element_by_id(doc, "view", 0, &view);
    CHECK_OK(kui_document_router_register_view(doc, view), "register view");
    CHECK_OK(kui_document_router_register_view(doc, view), "register view twice is harmless");
    releases = 0;
    CHECK_OK(kui_document_router_add_route(doc, "/", 0, "home", 0, build_view, NULL, count_release), "add /");
    CHECK_OK(kui_document_router_add_route(doc, "/users/:id", 0, NULL, 0, build_view, NULL, count_release),
             "add /users/:id");
    CHECK_OK(kui_document_router_add_route(doc, "/admin", 0, NULL, 0, build_view, NULL, count_release),
             "add /admin");
    CHECK(kui_document_router_add_route(doc, "/", 0, NULL, 0, build_view, NULL, count_release) ==
              KUI_ERR_INVALID_ARG, "duplicate pattern is rejected");
    CHECK(releases == 0, "a rejected route does not release user_data");
    uint32_t count = 0;
    kui_document_router_get_route_count(doc, &count);
    CHECK(count == 3, "three routes");
    int has = 0;
    kui_document_router_has_route(doc, "/users/:id", 0, &has);
    CHECK(has == 1, "has_route");

    uint32_t guard_id = 0, nav_id = 0;
    CHECK_OK(kui_document_router_add_guard(doc, KUI_C_ROUTE_GUARD_BEFORE_ENTER, guard_enter, NULL,
                                           count_release, &guard_id), "add guard");
    CHECK(kui_document_router_add_guard(doc, (KuiCRouteGuardKind)7, guard_enter, NULL, count_release,
                                        &guard_id) == KUI_ERR_INVALID_ARG, "guard kind validated");
    CHECK_OK(kui_document_router_add_navigated_callback(doc, on_navigated, NULL, count_release, &nav_id),
             "add navigated callback");

    int navigated = 0;
    int history_index = -2, history_size = -1;
    kui_document_router_get_history(doc, &history_index, &history_size);
    CHECK(history_index == -1 && history_size == 0, "empty history");
    CHECK_OK(kui_document_router_push(doc, "/users/42?tab=info", 0, &navigated), "push user");
    CHECK(navigated == 1 && navigations == 1, "navigated once");
    CHECK(strcmp(last_query_tab, "info") == 0, "query params reach the callback");
    char param[32];
    size_t len = 0;
    CHECK_OK(kui_document_router_get_param(doc, "id", 0, param, sizeof param, &len), "get param");
    CHECK(strcmp(param, "42") == 0, "param value");
    CHECK(kui_document_router_get_param(doc, "nope", 0, param, sizeof param, &len) == KUI_ERR_NOT_FOUND,
          "missing param");
    char path[64];
    kui_document_router_get_current_path(doc, path, sizeof path, &len);
    CHECK(strcmp(path, "/users/42?tab=info") == 0, "current path");
    CHECK_OK(kui_document_router_get_current(doc, read_current, NULL), "get current");
    CHECK(strcmp(current_pattern, "/users/:id") == 0, "current pattern");
    pump(5);
    KuiElement* routed = NULL;
    CHECK_OK(kui_document_get_element_by_id(doc, "routed", 0, &routed), "view element is in the page");
    KuiElement* auto_view = NULL;
    size_t auto_children = 0;
    kui_document_get_element_by_id(doc, "auto", 0, &auto_view);
    if (auto_view) {
        kui_element_child_count(auto_view, &auto_children);
        kui_element_release(auto_view);
    }
    CHECK(auto_children == 1, "<kui-router-view> registers itself");
    if (routed) {
        CHECK(text_is(routed, "view:/users/:id id=42"), "view built from the route");
        kui_element_release(routed);
    }

    CHECK_OK(kui_document_router_push(doc, "/admin", 0, &navigated), "push admin");
    CHECK(navigated == 0 && guard_calls >= 2, "guard cancels /admin");
    CHECK_OK(kui_document_router_remove_guard(doc, guard_id), "remove guard");
    CHECK(releases == 1, "removing the guard releases its user_data");
    CHECK_OK(kui_document_router_push(doc, "/admin", 0, &navigated), "push admin again");
    CHECK(navigated == 1, "guard gone");
    CHECK_OK(kui_document_router_go(doc, -1, &navigated), "back");
    CHECK(navigated == 1, "went back");
    kui_document_router_get_current_path(doc, path, sizeof path, &len);
    CHECK(strcmp(path, "/users/42?tab=info") == 0, "back to the user page");
    CHECK_OK(kui_document_router_go(doc, -5, &navigated), "go out of range");
    CHECK(navigated == 0, "out-of-range go does not navigate");
    CHECK_OK(kui_document_router_replace(doc, "/", 0, &navigated), "replace");
    kui_document_router_get_history(doc, &history_index, &history_size);
    CHECK(history_index == 0 && history_size == 2, "replace keeps the history size");

    CHECK_OK(kui_document_router_remove_navigated_callback(doc, nav_id), "remove navigated callback");
    CHECK(releases == 2, "removing the callback releases its user_data");
    CHECK(kui_document_router_remove_route(doc, "/missing", 0) == KUI_ERR_NOT_FOUND, "remove unknown");
    CHECK_OK(kui_document_router_remove_route(doc, "/admin", 0), "remove /admin");
    CHECK(releases == 3, "removed route releases its user_data");
    CHECK_OK(kui_document_router_clear_routes(doc), "clear routes");
    /* "/" is still the current view factory, so its user_data stays referenced. */
    CHECK(releases == 4, "clear releases the routes that are not the current view");
    CHECK(views_built >= 2, "views were built");
    CHECK_OK(kui_document_router_unregister_view(doc, view), "unregister view");
    CHECK(kui_document_router_unregister_view(doc, view) == KUI_ERR_NOT_FOUND, "unregister twice");
    kui_element_release(view);
}

/* -- Data binding ------------------------------------------------------------- */
static int changes = 0;
static void on_change(void* user_data) {
    (void)user_data;
    ++changes;
}

static KuiObservable* drop_me = NULL;
static void release_inside(void* user_data) {
    (void)user_data;
    if (drop_me) {
        kui_observable_release(drop_me); /* the last reference, from inside notify */
        drop_me = NULL;
    }
}

static void test_binding(KuiDocument* doc) {
    kui_document_load_html(doc, "<html><body><p id='label'>old</p><input id='name'></body></html>", 0);
    pump(3);
    KuiElement* label = NULL;
    KuiElement* input = NULL;
    kui_document_get_element_by_id(doc, "label", 0, &label);
    kui_document_get_element_by_id(doc, "name", 0, &input);

    KuiObservable* obs = NULL;
    CHECK_OK(kui_observable_create("hello", 0, &obs), "create observable");
    char buf[64];
    size_t len = 0;
    kui_observable_get(obs, buf, sizeof buf, &len);
    CHECK(strcmp(buf, "hello") == 0 && len == 5, "initial value");

    releases = 0;
    uint32_t sub = 0;
    CHECK_OK(kui_observable_subscribe(obs, on_change, NULL, count_release, &sub), "subscribe");
    uint32_t text_binding = 0, attr_binding = 0, input_binding = 0;
    CHECK_OK(kui_observable_bind(obs, label, "textContent", 0, 0, &text_binding), "bind textContent");
    CHECK(text_is(label, "hello"), "binding sets the property right away");
    CHECK_OK(kui_observable_bind(obs, label, "data-name", 0, 0, &attr_binding), "bind attribute");
    CHECK_OK(kui_observable_bind(obs, input, "value", 0, 1, &input_binding), "two-way bind");
    CHECK(kui_observable_bind(obs, label, "", 0, 0, &attr_binding) == KUI_ERR_INVALID_ARG,
          "empty property");

    CHECK_OK(kui_observable_set(obs, "world", 0), "set");
    CHECK(changes == 1, "subscriber notified");
    CHECK_OK(kui_observable_set(obs, "world", 0), "set same value");
    CHECK(changes == 1, "no notification without a change");
    CHECK_OK(kui_document_flush_bindings(doc), "flush");
    CHECK(text_is(label, "world"), "textContent follows");
    kui_element_get_attribute(label, "data-name", 0, buf, sizeof buf, &len);
    CHECK(strcmp(buf, "world") == 0, "attribute follows");

    CHECK(kui_observable_unsubscribe(obs, 9999) == KUI_ERR_NOT_FOUND, "unknown subscription");
    CHECK_OK(kui_observable_unsubscribe(obs, sub), "unsubscribe");
    CHECK(releases == 1, "unsubscribe releases user_data");
    CHECK_OK(kui_observable_unbind(obs, text_binding), "unbind");
    CHECK(kui_observable_unbind(obs, text_binding) == KUI_ERR_NOT_FOUND, "unbind twice");
    kui_observable_set(obs, "again", 0);
    kui_document_flush_bindings(doc);
    CHECK(text_is(label, "world"), "unbound property stays");
    CHECK_OK(kui_observable_unbind_all(obs), "unbind all");

    /* Releasing the last reference from inside a subscriber is safe. */
    CHECK_OK(kui_observable_subscribe(obs, release_inside, NULL, count_release, &sub), "subscribe dropper");
    drop_me = obs;
    CHECK_OK(kui_observable_set(obs, "bye", 0), "set that drops the observable");
    CHECK(drop_me == NULL && releases == 2, "dropped inside notify; subscription released");

    KuiObservable* empty = NULL;
    CHECK_OK(kui_observable_create(NULL, 0, &empty), "create with NULL initial");
    kui_observable_get(empty, buf, sizeof buf, &len);
    CHECK(len == 0, "empty initial value");
    CHECK_OK(kui_observable_retain(empty), "retain");
    CHECK_OK(kui_observable_release(empty), "release 1");
    CHECK_OK(kui_observable_release(empty), "release 2");

    kui_element_release(label);
    kui_element_release(input);
}

/* -- Animation state ---------------------------------------------------------- */
static void test_animation(KuiDocument* doc) {
    kui_document_load_html(doc,
        "<html><head><style>@keyframes pulse{from{opacity:0}to{opacity:1}}"
        "#spin{animation:pulse 1s infinite}</style></head>"
        "<body><div id='spin'>x</div><div id='still'>y</div></body></html>", 0);
    KuiElement* spin = NULL;
    KuiElement* still = NULL;
    kui_document_get_element_by_id(doc, "spin", 0, &spin);
    kui_document_get_element_by_id(doc, "still", 0, &still);
    int animating = 0, transitioning = 0;
    for (int i = 0; i < 100 && !animating; ++i) {
        kui_platform_wait_events(0.01);
        kui_element_get_animation_state(spin, &animating, &transitioning);
    }
    CHECK(animating == 1 && transitioning == 0, "CSS animation is reported");
    kui_element_get_animation_state(still, &animating, &transitioning);
    CHECK(animating == 0 && transitioning == 0, "static element");
    uint32_t animations = 0, transitions = 9;
    CHECK_OK(kui_document_get_animation_counts(doc, &animations, &transitions), "counts");
    CHECK(animations >= 1 && transitions == 0, "one running animation");
    CHECK_OK(kui_element_get_animation_state(still, NULL, NULL), "NULL outputs are allowed");
    kui_element_release(spin);
    kui_element_release(still);
}

/* -- Observers ---------------------------------------------------------------- */
static int mutation_batches = 0;
static int saw_child = 0, saw_text = 0, saw_attribute = 0;
static char old_value[32];
static void on_mutation(const KuiCMutationRecord* records, size_t count, void* user_data) {
    (void)user_data;
    ++mutation_batches;
    for (size_t i = 0; i < count; ++i) {
        const KuiCMutationRecord* r = &records[i];
        if (r->type == KUI_C_MUTATION_CHILD_LIST && r->added && r->added_node_type == 1) saw_child = 1;
        if (r->type == KUI_C_MUTATION_CHILD_LIST && !r->added && r->added_node_type == 3) saw_text = 1;
        if (r->type == KUI_C_MUTATION_ATTRIBUTES && r->target_node_type == 1 &&
            r->attribute_name_len == 5 && memcmp(r->attribute_name, "title", 5) == 0) {
            saw_attribute = 1;
            snprintf(old_value, sizeof old_value, "%.*s", (int)r->old_value_len, r->old_value);
        }
    }
}

static KuiObserver* self_disconnect = NULL;
static int self_disconnect_calls = 0;
static void disconnect_inside(const KuiCMutationRecord* records, size_t count, void* user_data) {
    (void)records;
    (void)count;
    (void)user_data;
    ++self_disconnect_calls;
    kui_observer_release(self_disconnect); /* last reference, from inside the callback */
}

static int resize_calls = 0;
static float resized_width = 0;
static void on_resize(const KuiCResizeEntry* entries, size_t count, void* user_data) {
    (void)user_data;
    ++resize_calls;
    if (count) resized_width = entries[count - 1].content_width;
}

static int intersection_calls = 0;
static int intersecting = -1;
static void on_intersection(const KuiCIntersectionEntry* entries, size_t count, void* user_data) {
    (void)user_data;
    ++intersection_calls;
    if (count) intersecting = entries[count - 1].is_intersecting;
}

static void test_observers(KuiDocument* doc) {
    kui_document_load_html(doc,
        "<html><body style='margin:0'><div id='box' title='a' style='width:120px;height:40px'></div>"
        "<div id='far' style='position:absolute;top:5000px;width:10px;height:10px'></div></body></html>", 0);
    pump(5);
    KuiElement* body = NULL;
    KuiElement* box = NULL;
    KuiElement* far = NULL;
    kui_document_body(doc, &body);
    kui_document_get_element_by_id(doc, "box", 0, &box);
    kui_document_get_element_by_id(doc, "far", 0, &far);

    releases = 0;
    KuiObserver* mutation = NULL;
    CHECK_OK(kui_mutation_observer_create(on_mutation, NULL, count_release, &mutation), "mutation create");
    KuiCMutationOptions options;
    memset(&options, 0, sizeof options);
    CHECK(kui_mutation_observer_observe(mutation, body, &options) == KUI_ERR_INVALID_ARG,
          "needs at least one kind");
    options.child_list = 1;
    options.attributes = 1;
    options.subtree = 1;
    options.attribute_old_value = 1;
    const char* filter[] = {"title"};
    options.attribute_filter = filter;
    options.attribute_filter_count = 1;
    CHECK_OK(kui_mutation_observer_observe(mutation, body, &options), "mutation observe");
    CHECK(kui_observer_observe(mutation, body) == KUI_ERR_INVALID_ARG, "mutation needs options");
    CHECK(kui_observer_unobserve(mutation, body) == KUI_ERR_NOT_SUPPORTED, "no per-target unobserve");

    KuiElement* child = NULL;
    kui_document_create_element(doc, "span", 0, &child);
    kui_element_append_child(box, child);
    kui_element_append_text(box, "t", 0);
    kui_element_set_attribute(box, "title", 0, "b", 0);
    kui_element_set_attribute(box, "lang", 0, "x", 0); /* filtered out */
    CHECK(mutation_batches >= 3, "records arrive synchronously");
    CHECK(saw_child && saw_text, "child list records with node types");
    CHECK(saw_attribute && strcmp(old_value, "a") == 0, "attribute record with old value");
    CHECK_OK(kui_observer_disconnect(mutation), "disconnect");
    const int before = mutation_batches;
    kui_element_set_attribute(box, "title", 0, "c", 0);
    CHECK(mutation_batches == before, "nothing after disconnect");
    CHECK_OK(kui_observer_retain(mutation), "retain");
    CHECK_OK(kui_observer_release(mutation), "release 1");
    CHECK(releases == 0, "still referenced");
    CHECK_OK(kui_observer_release(mutation), "release 2");
    CHECK(releases == 1, "last release calls release_fn");

    /* Dropping the last reference from inside the callback. */
    CHECK_OK(kui_mutation_observer_create(disconnect_inside, NULL, count_release, &self_disconnect),
             "second mutation observer");
    CHECK_OK(kui_mutation_observer_observe(self_disconnect, body, &options), "observe");
    kui_element_set_attribute(box, "title", 0, "d", 0);
    kui_element_set_attribute(box, "title", 0, "e", 0);
    CHECK(self_disconnect_calls == 1, "released observer gets nothing more");
    pump(3); /* the deferred free runs from the main-thread queue */
    CHECK(releases == 2, "deferred free calls release_fn");

    KuiObserver* resize = NULL;
    CHECK_OK(kui_resize_observer_create(on_resize, NULL, count_release, &resize), "resize create");
    CHECK_OK(kui_observer_observe(resize, box), "resize observe");
    for (int i = 0; i < 100 && resize_calls == 0; ++i) kui_platform_wait_events(0.01);
    CHECK(resize_calls >= 1 && resized_width == 120.0f, "first observation reports the size");
    kui_element_set_style_property(box, "width", 0, "200px", 0, 0);
    for (int i = 0; i < 100 && resized_width != 200.0f; ++i) kui_platform_wait_events(0.01);
    CHECK(resized_width == 200.0f, "size change reported");
    CHECK_OK(kui_observer_unobserve(resize, box), "resize unobserve");
    CHECK_OK(kui_observer_release(resize), "resize release");

    float bad_threshold = 2.0f;
    KuiCIntersectionOptions io;
    memset(&io, 0, sizeof io);
    io.thresholds = &bad_threshold;
    io.threshold_count = 1;
    KuiObserver* intersection = NULL;
    const int releases_before = releases;
    CHECK(kui_intersection_observer_create(&io, on_intersection, NULL, count_release, &intersection) ==
              KUI_ERR_INVALID_ARG, "threshold out of range");
    CHECK(releases == releases_before, "failed create does not release user_data");
    CHECK_OK(kui_intersection_observer_create(NULL, on_intersection, NULL, count_release, &intersection),
             "intersection create");
    CHECK_OK(kui_observer_observe(intersection, box), "observe visible box");
    for (int i = 0; i < 100 && intersection_calls == 0; ++i) kui_platform_wait_events(0.01);
    CHECK(intersecting == 1, "visible box intersects");
    intersection_calls = 0;
    CHECK_OK(kui_observer_observe(intersection, far), "observe far box");
    for (int i = 0; i < 100 && intersection_calls == 0; ++i) kui_platform_wait_events(0.01);
    CHECK(intersecting == 0, "far box does not intersect");
    CHECK_OK(kui_observer_release(intersection), "intersection release");

    kui_element_release(child);
    kui_element_release(far);
    kui_element_release(box);
    kui_element_release(body);
}

int main(void) {
    CHECK_OK(kui_platform_init(), "platform init");
    KuiWindowProps props;
    memset(&props, 0, sizeof props);
    props.title = "kui-c-framework";
    props.width = 640;
    props.height = 480;
    KuiWindow* window = NULL;
    CHECK_OK(kui_window_create(&props, &window), "create window");
    if (window) {
        KuiDocument* doc = NULL;
        kui_window_get_document(window, &doc);
        test_router(doc);
        test_binding(doc);
        test_animation(doc);
        test_observers(doc);
        releases = 0;
        kui_document_release(doc);
        CHECK_OK(kui_window_release(window), "release window");
    }
    kui_platform_shutdown();
    printf("kui_c framework: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
