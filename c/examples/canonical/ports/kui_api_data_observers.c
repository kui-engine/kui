/* API tour: observable values and bindings, Mutation / Resize / Intersection observers, the theme
 * manager and the document router (examples/api_tours/data_observers). On start the host calls
 * every function once, checks the results against kui_c.h and logs one row per check into
 * #tour-log. Resize and intersection entries only arrive after rendered frames, so those checks
 * run from a chain of timers and #tour-result becomes "PASS n/n" at the end (about 1 s).
 * The panel buttons repeat the calls on the visible elements. */
#include "kui_c_example_dom.h"

#include <math.h>
#include <stdlib.h>
#include <stdarg.h>

static KuiDocument* g_doc;

/* ---- Self-check bookkeeping: count, log one <tr> per check, publish the verdict ----------
 * check():  a documented behaviour; a failure makes the tour FAIL.
 * known():  a documented behaviour the engine currently gets wrong (reported bug). A failure is
 *           logged as "KNOWN BUG" and kept out of the verdict; once it passes it counts as ok. */

static int g_total;
static int g_failed;
static int g_known;
static char g_failed_names[512];

static void log_row(const char* name, const char* result, const char* cell) {
    KuiElement* log = kuex_by_id(g_doc, "tour-log");
    KuiElement* row = NULL;
    if (!log) return;
    if (kui_document_create_element(g_doc, "tr", 0, &row) == KUI_OK) {
        kui_element_set_attribute(row, "data-check", 0, name, 0);
        kui_element_set_attribute(row, "data-result", 0, result, 0);
        if (strcmp(result, "ok") != 0) kui_element_class_list_add(row, "bad", 0);
        kuex_append_release(row, kuex_make_element(g_doc, "td", name, NULL));
        kuex_append_release(row, kuex_make_element(g_doc, "td", cell, NULL));
        kuex_append_release(log, row);
    }
    kui_element_release(log);
}

static void check(const char* name, int ok, const char* fmt, ...) {
    char detail[256];
    char cell[300];
    size_t used = strlen(g_failed_names);
    va_list args;

    g_total++;
    if (ok) {
        log_row(name, "ok", "ok");
        return;
    }
    va_start(args, fmt);
    vsnprintf(detail, sizeof detail, fmt, args);
    va_end(args);
    g_failed++;
    snprintf(g_failed_names + used, sizeof g_failed_names - used, "%s%s", used ? ", " : "", name);
    snprintf(cell, sizeof cell, "FAIL: %s", detail);
    fprintf(stderr, "tour check failed: %s: %s\n", name, detail);
    log_row(name, "fail", cell);
}

static void known(const char* name, int ok, const char* bug, const char* fmt, ...) {
    char detail[256];
    char cell[400];
    va_list args;

    if (ok) {
        g_total++;
        log_row(name, "ok", "ok");
        return;
    }
    va_start(args, fmt);
    vsnprintf(detail, sizeof detail, fmt, args);
    va_end(args);
    g_known++;
    snprintf(cell, sizeof cell, "KNOWN BUG: %s (%s)", bug, detail);
    fprintf(stderr, "tour known bug: %s: %s\n", name, detail);
    log_row(name, "known", cell);
}

static void finish(void) {
    char text[640];
    char number[16];
    size_t used;
    KuiElement* result = kuex_by_id(g_doc, "tour-result");
    if (!result) return;
    if (g_failed == 0) {
        snprintf(text, sizeof text, "PASS %d/%d", g_total, g_total);
    } else {
        snprintf(text, sizeof text, "FAIL %d/%d: %s", g_failed, g_total, g_failed_names);
    }
    used = strlen(text);
    if (g_known) snprintf(text + used, sizeof text - used, " (known bugs: %d)", g_known);
    kui_element_set_text_content(result, text, 0);
    snprintf(number, sizeof number, "%d", g_total);
    kui_element_set_attribute(result, "data-total", 0, number, 0);
    snprintf(number, sizeof number, "%d", g_failed);
    kui_element_set_attribute(result, "data-failed", 0, number, 0);
    snprintf(number, sizeof number, "%d", g_known);
    kui_element_set_attribute(result, "data-known", 0, number, 0);
    kui_element_class_list_add(result, g_failed == 0 ? "pass" : "fail", 0);
    kui_element_release(result);
}

/* ---- Small readers ------------------------------------------------------------------------ */

static void release(KuiElement* element) {
    if (element) kui_element_release(element);
}

static int near(float a, float b, float tolerance) {
    return fabsf(a - b) <= tolerance;
}

static void id_of(KuiElement* element, char* buf, size_t capacity) {
    size_t length = 0;
    buf[0] = '\0';
    if (element) kui_element_get_id(element, buf, capacity, &length);
    buf[capacity - 1] = '\0';
}

static void text_of(const char* id, char* buf, size_t capacity) {
    kuex_get_text(g_doc, id, buf, capacity);
}

static void attr_of(const char* id, const char* name, char* buf, size_t capacity) {
    kuex_get_attr(g_doc, id, name, buf, capacity);
}

static void computed(const char* id, const char* property, char* buf, size_t capacity) {
    size_t length = 0;
    KuiElement* element = kuex_by_id(g_doc, id);
    buf[0] = '\0';
    if (element) kui_element_get_computed_style_value(element, property, 0, buf, capacity, &length);
    buf[capacity - 1] = '\0';
    release(element);
}

/* A CSS colour as "#rrggbb" or "rgb(r, g, b)" / "rgba(r, g, b, a)". */
static int colour_is(const char* value, int r, int g, int b) {
    long got[3] = {-1, -1, -1};
    char* end = NULL;
    int index;
    if (value[0] == '#' && strlen(value) == 7) {
        long hex = strtol(value + 1, &end, 16);
        if (*end) return 0;
        got[0] = (hex >> 16) & 255;
        got[1] = (hex >> 8) & 255;
        got[2] = hex & 255;
    } else {
        const char* p = strchr(value, '(');
        if (strncmp(value, "rgb", 3) != 0 || !p) return 0;
        for (index = 0; index < 3; index++) {
            got[index] = strtol(p + 1, &end, 10);
            if (end == p + 1) return 0;
            p = end;
            while (*p == ' ') p++;
            if (index < 2 && *p != ',') return 0;
        }
    }
    return got[0] == r && got[1] == g && got[2] == b;
}

static KuiRect rect_of(const char* id) {
    KuiRect rect;
    KuiElement* element = kuex_by_id(g_doc, id);
    memset(&rect, 0, sizeof rect);
    if (element) kui_element_get_bounding_rect(element, &rect);
    release(element);
    return rect;
}

static void set_style(const char* id, const char* property, const char* value) {
    kuex_set_style(g_doc, id, property, value);
}

static void scroll_to(const char* id, float top) {
    KuiElement* element = kuex_by_id(g_doc, id);
    if (element) kui_element_set_scroll_position(element, 0, top);
    release(element);
}

static void observable_text(KuiObservable* obs, char* buf, size_t capacity) {
    size_t length = 0;
    buf[0] = '\0';
    if (obs) kui_observable_get(obs, buf, capacity, &length);
    buf[capacity - 1] = '\0';
}

static void input_value(const char* id, char* buf, size_t capacity) {
    kuex_get_value(g_doc, id, buf, capacity);
}

/* release_fn for the callbacks below: counts how often KUI dropped user_data. */
static void count_release(void* user_data) {
    (*(int*)user_data)++;
}

/* ---- Observable: values, subscriptions, one- and two-way bindings, flush, refcount -------- */

static int g_sub_calls;
static int g_sub_released;

static void on_change_count(void* user_data) {
    (void)user_data;
    g_sub_calls++;
}

static KuiStatus bind_id(KuiObservable* obs, const char* id, const char* property, int two_way,
                         uint32_t* out_id) {
    KuiStatus status = KUI_ERR_NOT_FOUND;
    KuiElement* element = kuex_by_id(g_doc, id);
    if (element) status = kui_observable_bind(obs, element, property, 0, two_way, out_id);
    release(element);
    return status;
}

static void check_observable(void) {
    KuiObservable* obs = NULL;
    KuiObservable* empty = NULL;
    KuiObservable* style = NULL;
    KuiObservable* input = NULL;
    uint32_t sub = 0, text_binding = 0, attr_binding = 0, unused = 0;
    char value[128], text[64], attr[64], css[64];
    size_t length = 99;
    KuiStatus status;

    status = kui_observable_create("0", 0, &obs);
    observable_text(obs, value, sizeof value);
    check("ob.create_get", status == KUI_OK && obs && strcmp(value, "0") == 0, "status %d, value '%s'",
          status, value);
    if (!obs) return;

    status = kui_observable_create(NULL, 0, &empty);
    if (empty) kui_observable_get(empty, value, sizeof value, &length);
    check("ob.create_empty", status == KUI_OK && length == 0, "status %d, length %u", status,
          (unsigned)length);
    if (empty) kui_observable_release(empty);

    kui_observable_set(obs, "1", 0);
    observable_text(obs, value, sizeof value);
    check("ob.set_get", strcmp(value, "1") == 0, "value '%s'", value);

    /* Subscribers run synchronously, and only when the value actually changed. */
    kui_observable_subscribe(obs, on_change_count, NULL, NULL, &sub);
    kui_observable_set(obs, "2", 0);
    check("ob.subscribe", g_sub_calls == 1, "calls after one change %d", g_sub_calls);
    kui_observable_set(obs, "2", 0);
    check("ob.set_unchanged", g_sub_calls == 1, "calls after setting the same value %d", g_sub_calls);
    status = kui_observable_unsubscribe(obs, sub);
    kui_observable_set(obs, "3", 0);
    check("ob.unsubscribe", status == KUI_OK && g_sub_calls == 1 &&
          kui_observable_unsubscribe(obs, sub) == KUI_ERR_NOT_FOUND,
          "status %d, calls %d", status, g_sub_calls);

    /* Binding writes the current value right away. */
    status = bind_id(obs, "s-text", "textContent", 0, &text_binding);
    text_of("s-text", text, sizeof text);
    check("ob.bind_text", status == KUI_OK && text_binding != 0 && strcmp(text, "3") == 0,
          "status %d, id %u, text '%s'", status, text_binding, text);

    /* Later changes are batched until the next frame or an explicit flush. */
    kui_observable_set(obs, "4", 0);
    text_of("s-text", text, sizeof text);
    check("ob.batched", strcmp(text, "3") == 0, "text right after set '%s' (want the old '3')", text);
    status = kui_document_flush_bindings(g_doc);
    text_of("s-text", text, sizeof text);
    check("ob.flush_bindings", status == KUI_OK && strcmp(text, "4") == 0, "status %d, text '%s'",
          status, text);

    /* Any other property name is an attribute. */
    status = bind_id(obs, "s-attr", "data-level", 0, &attr_binding);
    attr_of("s-attr", "data-level", attr, sizeof attr);
    check("ob.bind_attr", status == KUI_OK && strcmp(attr, "4") == 0, "status %d, data-level '%s'",
          status, attr);

    /* "style" is an attribute too: the value is the element's inline declaration. */
    kui_observable_create("background-color: #ff0000", 0, &style);
    status = style ? bind_id(style, "s-style", "style", 0, &unused) : KUI_ERR_INTERNAL;
    computed("s-style", "background-color", css, sizeof css);
    {
        int red = colour_is(css, 255, 0, 0);
        char green_css[64];
        if (style) kui_observable_set(style, "background-color: #00ff00", 0);
        kui_document_flush_bindings(g_doc);
        computed("s-style", "background-color", green_css, sizeof green_css);
        check("ob.bind_style", status == KUI_OK && red && colour_is(green_css, 0, 255, 0),
              "status %d, computed '%s' then '%s'", status, css, green_css);
    }

    /* Two-way: "value" on an <input> (user input writes back; typed by the test case). */
    kui_observable_create("seed", 0, &input);
    status = input ? bind_id(input, "s-input", "value", 1, &unused) : KUI_ERR_INTERNAL;
    input_value("s-input", value, sizeof value);
    {
        char after[64];
        int seeded = strcmp(value, "seed") == 0;
        if (input) kui_observable_set(input, "next", 0);
        kui_document_flush_bindings(g_doc);
        input_value("s-input", after, sizeof after);
        check("ob.bind_two_way", status == KUI_OK && seeded && strcmp(after, "next") == 0,
              "status %d, input value '%s' then '%s'", status, value, after);
    }

    status = bind_id(obs, "s-text", "", 0, &unused);
    check("ob.bind_invalid", status == KUI_ERR_INVALID_ARG, "empty property -> status %d", status);

    /* Unbinding one binding leaves the others. */
    status = kui_observable_unbind(obs, text_binding);
    kui_observable_set(obs, "5", 0);
    kui_document_flush_bindings(g_doc);
    text_of("s-text", text, sizeof text);
    attr_of("s-attr", "data-level", attr, sizeof attr);
    check("ob.unbind", status == KUI_OK && strcmp(text, "4") == 0 && strcmp(attr, "5") == 0 &&
          kui_observable_unbind(obs, text_binding) == KUI_ERR_NOT_FOUND,
          "status %d, text '%s' (want 4), data-level '%s' (want 5)", status, text, attr);

    status = kui_observable_unbind_all(obs);
    kui_observable_set(obs, "6", 0);
    kui_document_flush_bindings(g_doc);
    attr_of("s-attr", "data-level", attr, sizeof attr);
    check("ob.unbind_all", status == KUI_OK && strcmp(attr, "5") == 0 &&
          kui_observable_unbind(obs, attr_binding) == KUI_ERR_NOT_FOUND,
          "status %d, data-level '%s' (want 5)", status, attr);

    /* Reference counting: one retain, two releases; the last one drops the subscriptions. */
    kui_observable_subscribe(obs, on_change_count, &g_sub_released, count_release, &sub);
    g_sub_released = 0;
    status = kui_observable_retain(obs);
    kui_observable_release(obs);
    kui_observable_set(obs, "7", 0);
    observable_text(obs, value, sizeof value);
    check("ob.retain_release", status == KUI_OK && strcmp(value, "7") == 0 && g_sub_released == 0,
          "status %d, value after one release '%s', released %d", status, value, g_sub_released);
    kui_observable_release(obs);
    check("ob.release_fn", g_sub_released == 1, "subscription release_fn ran %d times (want 1)",
          g_sub_released);

    if (style) kui_observable_release(style);
    if (input) kui_observable_release(input);
}

/* ---- MutationObserver: child list, attributes, character data, filter, disconnect -------- */

typedef struct {
    int batches;
    int records;
    int type;
    char target[32];
    int target_node_type;
    char added[32];
    int added_node_type;
    char removed[32];
    char attribute[32];
    char old_value[32];
} MutationLog;

static MutationLog g_mlog;
static int g_mut_released;

static void remember_mutations(const KuiCMutationRecord* records, size_t count, void* user_data) {
    MutationLog* log = (MutationLog*)user_data;
    const KuiCMutationRecord* last;
    if (count == 0) return;
    last = &records[count - 1];
    log->batches++;
    log->records += (int)count;
    log->type = (int)last->type;
    id_of(last->target, log->target, sizeof log->target);
    log->target_node_type = last->target_node_type;
    id_of(last->added, log->added, sizeof log->added);
    log->added_node_type = last->added_node_type;
    id_of(last->removed, log->removed, sizeof log->removed);
    snprintf(log->attribute, sizeof log->attribute, "%.*s", (int)last->attribute_name_len,
             last->attribute_name ? last->attribute_name : "");
    snprintf(log->old_value, sizeof log->old_value, "%.*s", (int)last->old_value_len,
             last->old_value ? last->old_value : "");
}

/* release_fn of the self-check observer: KUI hands back its user_data (the log) exactly once. */
static void mutation_released(void* user_data) {
    if (user_data == &g_mlog) g_mut_released++;
}

static void set_attr(const char* id, const char* name, const char* value) {
    KuiElement* element = kuex_by_id(g_doc, id);
    if (element) kui_element_set_attribute(element, name, 0, value, 0);
    release(element);
}

static void check_mutation(void) {
    KuiObserver* obs = NULL;
    KuiCMutationOptions options;
    KuiElement* root = kuex_by_id(g_doc, "s-mut");
    KuiElement* list = kuex_by_id(g_doc, "s-list");
    KuiElement* item = NULL;
    const char* filter[1] = {"data-state"};
    KuiStatus status;
    int before;

    if (!root || !list) {
        check("mut.page", 0, "missing #s-mut / #s-list");
        release(root);
        release(list);
        return;
    }
    status = kui_mutation_observer_create(remember_mutations, &g_mlog, mutation_released, &obs);
    check("mut.create", status == KUI_OK && obs, "status %d", status);
    if (!obs) {
        release(root);
        release(list);
        return;
    }
    g_mut_released = 0;

    memset(&options, 0, sizeof options);
    status = kui_mutation_observer_observe(obs, root, &options);
    check("mut.observe_needs_kind", status == KUI_ERR_INVALID_ARG,
          "no child_list / attributes / character_data -> status %d", status);
    options.child_list = 1;
    options.attributes = 1;
    options.character_data = 1;
    options.subtree = 1;
    options.attribute_old_value = 1;
    options.character_data_old_value = 1;
    status = kui_mutation_observer_observe(obs, root, &options);
    check("mut.observe", status == KUI_OK && g_mlog.batches == 0, "status %d, batches %d", status,
          g_mlog.batches);

    /* A new child in the subtree: one childList record, delivered synchronously. */
    item = kuex_make_element(g_doc, "li", "one", NULL);
    if (item) {
        kui_element_set_attribute(item, "id", 0, "s-li-1", 0);
        kui_element_append_child(list, item);
    }
    check("mut.child_list_add", g_mlog.records == 1 && g_mlog.type == KUI_C_MUTATION_CHILD_LIST &&
          strcmp(g_mlog.target, "s-list") == 0 && strcmp(g_mlog.added, "s-li-1") == 0 &&
          g_mlog.added_node_type == 1, "records %d, type %d, target '%s', added '%s' (type %d)",
          g_mlog.records, g_mlog.type, g_mlog.target, g_mlog.added, g_mlog.added_node_type);

    if (item) kui_element_remove(item);
    check("mut.child_list_remove", g_mlog.records == 2 && g_mlog.type == KUI_C_MUTATION_CHILD_LIST &&
          strcmp(g_mlog.removed, "s-li-1") == 0 && strcmp(g_mlog.target, "s-list") == 0,
          "records %d, type %d, removed '%s', target '%s'", g_mlog.records, g_mlog.type,
          g_mlog.removed, g_mlog.target);
    release(item);

    set_attr("s-mitem", "data-state", "on");
    check("mut.attributes", g_mlog.records == 3 && g_mlog.type == KUI_C_MUTATION_ATTRIBUTES &&
          strcmp(g_mlog.target, "s-mitem") == 0 && strcmp(g_mlog.attribute, "data-state") == 0 &&
          strcmp(g_mlog.old_value, "off") == 0, "type %d, target '%s', name '%s', old '%s'",
          g_mlog.type, g_mlog.target, g_mlog.attribute, g_mlog.old_value);

    /* Rewriting the lone text child changes its data in place: a characterData record whose
     * target is the parent element, with the text node's type. */
    kuex_set_text(g_doc, "s-mtext", "world");
    check("mut.character_data", g_mlog.type == KUI_C_MUTATION_CHARACTER_DATA &&
          strcmp(g_mlog.target, "s-mtext") == 0 && g_mlog.target_node_type == 3 &&
          strcmp(g_mlog.old_value, "hello") == 0, "type %d, target '%s' (node type %d), old '%s'",
          g_mlog.type, g_mlog.target, g_mlog.target_node_type, g_mlog.old_value);

    /* Observing again replaces the options: attributes only, filtered to data-state. */
    memset(&options, 0, sizeof options);
    options.attributes = 1;
    options.subtree = 1;
    options.attribute_filter = filter;
    options.attribute_filter_count = 1;
    kui_mutation_observer_observe(obs, root, &options);
    before = g_mlog.records;
    set_attr("s-mitem", "title", "ignored");
    kuex_set_text(g_doc, "s-mtext", "again");
    {
        int filtered = g_mlog.records == before;
        set_attr("s-mitem", "data-state", "off");
        check("mut.attribute_filter", filtered && g_mlog.records == before + 1 &&
              strcmp(g_mlog.attribute, "data-state") == 0 && g_mlog.old_value[0] == '\0',
              "filtered %d, records %d -> %d, name '%s', old '%s' (not requested)", filtered, before,
              g_mlog.records, g_mlog.attribute, g_mlog.old_value);
    }

    status = kui_observer_observe(obs, root);
    {
        KuiStatus unobserve = kui_observer_unobserve(obs, root);
        check("mut.generic_observe", status == KUI_ERR_INVALID_ARG && unobserve == KUI_ERR_NOT_SUPPORTED,
              "observer_observe %d (want INVALID_ARG), observer_unobserve %d (want NOT_SUPPORTED)",
              status, unobserve);
    }

    /* One retain, one release: still observing. */
    status = kui_observer_retain(obs);
    kui_observer_release(obs);
    before = g_mlog.records;
    set_attr("s-mitem", "data-state", "on");
    check("mut.retain_release", status == KUI_OK && g_mlog.records == before + 1 && g_mut_released == 0,
          "status %d, records %d -> %d, released %d", status, before, g_mlog.records, g_mut_released);

    status = kui_observer_disconnect(obs);
    before = g_mlog.records;
    set_attr("s-mitem", "data-state", "off");
    check("mut.disconnect", status == KUI_OK && g_mlog.records == before, "status %d, records %d -> %d",
          status, before, g_mlog.records);

    kui_observer_release(obs);
    check("mut.release_fn", g_mut_released == 1, "release_fn ran %d times (want 1)", g_mut_released);
    release(root);
    release(list);
}

/* ---- Theme manager: built-ins, register / replace, apply, enumerate ---------------------- */

static KuiThemeManager* g_themes;

static const KuiCKeyValue g_light_vars[3] = {
    {"--tour-bg", 9, "#f0fdf4", 7}, {"--tour-fg", 9, "#14532d", 7}, {"--tour-accent", 13, "#22c55e", 7}};
static const KuiCKeyValue g_dark_first[3] = {
    {"--tour-bg", 9, "#0f172a", 7}, {"--tour-fg", 9, "#e2e8f0", 7}, {"--tour-accent", 13, "#38bdf8", 7}};
static const KuiCKeyValue g_dark_vars[3] = {
    {"--tour-bg", 9, "#0f172a", 7}, {"--tour-fg", 9, "#e2e8f0", 7}, {"--tour-accent", 13, "#f59e0b", 7}};

static void active_theme(char* buf, size_t capacity) {
    size_t length = 0;
    buf[0] = '\0';
    kui_theme_manager_get_active(g_themes, buf, capacity, &length);
    buf[capacity - 1] = '\0';
}

static int has_theme(const char* name) {
    size_t count = 0, index, length;
    char buf[64];
    kui_theme_manager_get_theme_count(g_themes, &count);
    for (index = 0; index < count; index++) {
        length = 0;
        if (kui_theme_manager_get_theme_name(g_themes, index, buf, sizeof buf, &length) == KUI_OK &&
            strcmp(buf, name) == 0) {
            return 1;
        }
    }
    return 0;
}

/* The value of one variable of a theme, found by enumerating (the order is unspecified). */
static int theme_variable(const char* theme, const char* name, char* value, size_t capacity) {
    size_t count = 0, index, name_len, value_len;
    char key[64];
    value[0] = '\0';
    if (kui_theme_manager_get_variable_count(g_themes, theme, 0, &count) != KUI_OK) return 0;
    for (index = 0; index < count; index++) {
        if (kui_theme_manager_get_variable(g_themes, theme, 0, index, key, sizeof key, &name_len, value,
                                           capacity, &value_len) == KUI_OK &&
            strcmp(key, name) == 0) {
            return 1;
        }
    }
    value[0] = '\0';
    return 0;
}

static void html_custom_property(const char* name, char* buf, size_t capacity) {
    size_t length = 0;
    KuiElement* html = NULL;
    buf[0] = '\0';
    if (kui_document_query_selector(g_doc, "html", 0, &html) == KUI_OK && html) {
        kui_element_get_style_property(html, name, 0, buf, capacity, &length);
        kui_element_release(html);
    }
    buf[capacity - 1] = '\0';
}

static void check_theme(void) {
    KuiThemeManager* other = NULL;
    size_t count = 0, vars = 0, name_len = 0, value_len = 0, index;
    char active[64], value[64], key[64], css[64];
    int all_custom = 1;
    KuiStatus status;

    status = kui_theme_manager_create(&g_themes);
    kui_theme_manager_get_theme_count(g_themes, &count);
    check("th.builtin", status == KUI_OK && count == 2 && has_theme("light") && has_theme("dark"),
          "status %d, count %u, light %d, dark %d", status, (unsigned)count, has_theme("light"),
          has_theme("dark"));
    if (!g_themes) return;

    kui_theme_manager_get_variable_count(g_themes, "dark", 0, &vars);
    for (index = 0; index < vars; index++) {
        kui_theme_manager_get_variable(g_themes, "dark", 0, index, key, sizeof key, &name_len, value,
                                       sizeof value, &value_len);
        if (strncmp(key, "--", 2) != 0 || name_len != strlen(key)) all_custom = 0;
    }
    check("th.builtin_variables", vars > 0 && all_custom, "dark has %u variables, all custom properties %d",
          (unsigned)vars, all_custom);

    active_theme(active, sizeof active);
    check("th.active_empty", active[0] == '\0', "active before any apply '%s'", active);

    kui_theme_manager_register(g_themes, "tour-light", 0, g_light_vars, 3);
    status = kui_theme_manager_register(g_themes, "tour-dark", 0, g_dark_first, 3);
    kui_theme_manager_get_theme_count(g_themes, &count);
    vars = 0;
    kui_theme_manager_get_variable_count(g_themes, "tour-light", 0, &vars);
    check("th.register", status == KUI_OK && count == 4 && has_theme("tour-dark") && vars == 3,
          "status %d, count %u, tour-light variables %u", status, (unsigned)count, (unsigned)vars);

    theme_variable("tour-light", "--tour-fg", value, sizeof value);
    check("th.get_variable", strcmp(value, "#14532d") == 0, "tour-light --tour-fg '%s'", value);

    /* Registering an existing name replaces it. */
    kui_theme_manager_register(g_themes, "tour-dark", 0, g_dark_vars, 3);
    kui_theme_manager_get_theme_count(g_themes, &count);
    theme_variable("tour-dark", "--tour-accent", value, sizeof value);
    check("th.register_replace", count == 4 && strcmp(value, "#f59e0b") == 0,
          "count %u, --tour-accent '%s'", (unsigned)count, value);

    status = kui_theme_manager_get_variable_count(g_themes, "nope", 0, &vars);
    {
        KuiStatus range = kui_theme_manager_get_variable(g_themes, "tour-dark", 0, 3, key, sizeof key,
                                                         &name_len, value, sizeof value, &value_len);
        KuiStatus name = kui_theme_manager_get_theme_name(g_themes, 4, key, sizeof key, &name_len);
        check("th.not_found", status == KUI_ERR_NOT_FOUND && range == KUI_ERR_NOT_FOUND &&
              name == KUI_ERR_NOT_FOUND, "unknown theme %d, variable index 3 -> %d, theme index 4 -> %d",
              status, range, name);
    }

    /* Applying sets the variables inline on <html>; the card picks them up through var(). */
    status = kui_theme_manager_apply(g_themes, "tour-dark", 0, g_doc);
    active_theme(active, sizeof active);
    html_custom_property("--tour-bg", value, sizeof value);
    computed("theme-card", "background-color", css, sizeof css);
    check("th.apply", status == KUI_OK && strcmp(active, "tour-dark") == 0 && strcmp(value, "#0f172a") == 0,
          "status %d, active '%s', <html> --tour-bg '%s'", status, active, value);
    check("th.apply_computed", colour_is(css, 0x0f, 0x17, 0x2a), "card background-color '%s'", css);

    status = kui_theme_manager_apply(g_themes, "nope", 0, g_doc);
    active_theme(active, sizeof active);
    check("th.apply_unknown", status == KUI_ERR_NOT_FOUND && strcmp(active, "tour-dark") == 0,
          "status %d, active '%s'", status, active);

    kui_theme_manager_apply(g_themes, "tour-light", 0, g_doc);
    computed("theme-card", "background-color", css, sizeof css);
    check("th.switch", colour_is(css, 0xf0, 0xfd, 0xf4), "card background-color after tour-light '%s'", css);

    status = kui_theme_manager_create(&other);
    {
        KuiStatus released = other ? kui_theme_manager_release(other) : KUI_ERR_INTERNAL;
        check("th.release", status == KUI_OK && released == KUI_OK, "create %d, release %d", status,
              released);
    }
}

/* ---- Router: routes, views, navigation, history, params, guards, callbacks --------------- */

enum { VIEW_HOME, VIEW_USER, VIEW_ABOUT, VIEW_ADMIN };
static int g_view_kinds[4] = {VIEW_HOME, VIEW_USER, VIEW_ABOUT, VIEW_ADMIN};
static int g_nav_calls;
static int g_route_released;
static char g_nav_path[96];
static char g_guard_seen[96];
static int g_block_leave_about;
static int g_admin_locked = 1;

static void route_param(const KuiCRouteInfo* route, const char* name, char* buf, size_t capacity) {
    size_t index;
    buf[0] = '\0';
    for (index = 0; index < route->param_count; index++) {
        if (route->params[index].key_len == strlen(name) &&
            strncmp(route->params[index].key, name, route->params[index].key_len) == 0) {
            snprintf(buf, capacity, "%.*s", (int)route->params[index].value_len, route->params[index].value);
        }
    }
}

/* Builds the view for the current route; the returned reference is handed over to KUI. */
static KuiElement* make_view(KuiDocument* doc, const KuiCRouteInfo* route, void* user_data) {
    static const char* const ids[4] = {"view-home", "view-user", "view-about", "view-admin"};
    static const char* const titles[4] = {"Home", "User", "About", "Admin area"};
    int kind = *(const int*)user_data;
    char text[64], id[32];
    KuiElement* view = NULL;
    if (kui_document_create_element(doc, "div", 0, &view) != KUI_OK) return NULL;
    kui_element_set_attribute(view, "id", 0, ids[kind], 0);
    kui_element_set_attribute(view, "class", 0, "view", 0);
    if (kind == VIEW_USER) {
        route_param(route, "id", id, sizeof id);
        snprintf(text, sizeof text, "User %s", id);
    } else {
        snprintf(text, sizeof text, "%s", titles[kind]);
    }
    kui_element_set_text_content(view, text, 0);
    return view;
}

static void on_navigated(const KuiCRouteInfo* route, void* user_data) {
    char line[160];
    (void)user_data;
    g_nav_calls++;
    snprintf(g_nav_path, sizeof g_nav_path, "%.*s", (int)route->path_len, route->path);
    snprintf(line, sizeof line, "navigated: %.*s (pattern %.*s)", (int)route->path_len, route->path,
             (int)route->pattern_len, route->pattern);
    kuex_set_text(g_doc, "route-nav", line);
}

static int path_is(const KuiCRouteInfo* route, const char* path) {
    return route->path_len == strlen(path) && strncmp(route->path, path, route->path_len) == 0;
}

/* Self-check guard: refuses to enter /admin and remembers what it was asked. */
static int guard_enter(const KuiCRouteInfo* from, const KuiCRouteInfo* to, void* user_data) {
    (void)user_data;
    snprintf(g_guard_seen, sizeof g_guard_seen, "%.*s -> %.*s", (int)from->path_len, from->path,
             (int)to->path_len, to->path);
    return !path_is(to, "/admin");
}

/* Self-check guard: refuses to leave /about for / while g_block_leave_about is set. */
static int guard_leave(const KuiCRouteInfo* from, const KuiCRouteInfo* to, void* user_data) {
    (void)user_data;
    return !(g_block_leave_about && path_is(from, "/about") && path_is(to, "/"));
}

/* Panel guard: /admin stays closed while locked. */
static int guard_admin(const KuiCRouteInfo* from, const KuiCRouteInfo* to, void* user_data) {
    (void)from;
    (void)user_data;
    return !(g_admin_locked && path_is(to, "/admin"));
}

typedef struct {
    char path[96];
    char pattern[64];
    char query[64];
    char tab[16];
    size_t params;
} RouteCopy;

static void copy_route(const KuiCRouteInfo* route, void* user_data) {
    RouteCopy* copy = (RouteCopy*)user_data;
    size_t index;
    snprintf(copy->path, sizeof copy->path, "%.*s", (int)route->path_len, route->path);
    snprintf(copy->pattern, sizeof copy->pattern, "%.*s", (int)route->pattern_len, route->pattern);
    snprintf(copy->query, sizeof copy->query, "%.*s", (int)route->query_len, route->query);
    copy->params = route->param_count;
    copy->tab[0] = '\0';
    for (index = 0; index < route->query_param_count; index++) {
        if (route->query_params[index].key_len == 3 && strncmp(route->query_params[index].key, "tab", 3) == 0) {
            snprintf(copy->tab, sizeof copy->tab, "%.*s", (int)route->query_params[index].value_len,
                     route->query_params[index].value);
        }
    }
}

static void current_path(char* buf, size_t capacity) {
    size_t length = 0;
    buf[0] = '\0';
    kui_document_router_get_current_path(g_doc, buf, capacity, &length);
    buf[capacity - 1] = '\0';
}

static int push(const char* path) {
    int navigated = -1;
    kui_document_router_push(g_doc, path, 0, &navigated);
    return navigated;
}

static void history(int* index, int* size) {
    *index = -2;
    *size = -2;
    kui_document_router_get_history(g_doc, index, size);
}

/* The id of the view element the router put into #route-view ("" when empty). */
static void view_id(char* buf, size_t capacity) {
    KuiElement* view = kuex_by_id(g_doc, "route-view");
    KuiElement* child = NULL;
    buf[0] = '\0';
    if (view && kui_element_first_child(view, &child) == KUI_OK && child) {
        id_of(child, buf, capacity);
        kui_element_release(child);
    }
    release(view);
}

static size_t view_children(void) {
    size_t count = 99;
    KuiElement* view = kuex_by_id(g_doc, "route-view");
    if (view) kui_element_child_count(view, &count);
    release(view);
    return count;
}

/* release_fn of the "/" route: KUI hands back the route's user_data once the route is gone. */
static void route_released(void* user_data) {
    (void)user_data;
    g_route_released++;
}

static KuiStatus add_routes(void) {
    KuiStatus status = KUI_OK;
    status |= kui_document_router_add_route(g_doc, "/", 0, "home", 0, make_view, &g_view_kinds[VIEW_HOME],
                                            route_released);
    status |= kui_document_router_add_route(g_doc, "/user/:id", 0, "user", 0, make_view,
                                            &g_view_kinds[VIEW_USER], NULL);
    status |= kui_document_router_add_route(g_doc, "/about", 0, "about", 0, make_view,
                                            &g_view_kinds[VIEW_ABOUT], NULL);
    status |= kui_document_router_add_route(g_doc, "/admin", 0, "admin", 0, make_view,
                                            &g_view_kinds[VIEW_ADMIN], NULL);
    return status;
}

static uint32_t g_admin_guard;
static uint32_t g_nav_callback;

static void check_router(void) {
    KuiElement* view = kuex_by_id(g_doc, "route-view");
    KuiStatus status;
    uint32_t count = 0, enter = 0, leave = 0;
    int has = -1, index, size, navigated, calls, released;
    char path[96], id[32], param[32];
    size_t length = 0;
    RouteCopy route;

    if (!view) {
        check("rt.page", 0, "missing #route-view");
        return;
    }
    history(&index, &size);
    check("rt.history_initial", index == -1 && size == 0, "index %d, size %d (want -1, 0)", index, size);

    status = add_routes();
    kui_document_router_get_route_count(g_doc, &count);
    kui_document_router_has_route(g_doc, "/user/:id", 0, &has);
    check("rt.add_route", status == KUI_OK && count == 4 && has == 1, "status %d, count %u, has /user/:id %d",
          status, count, has);
    status = kui_document_router_add_route(g_doc, "/about", 0, "again", 0, make_view,
                                           &g_view_kinds[VIEW_ABOUT], NULL);
    kui_document_router_has_route(g_doc, "/nope", 0, &has);
    check("rt.add_route_duplicate", status == KUI_ERR_INVALID_ARG && has == 0,
          "duplicate -> %d, has /nope %d", status, has);

    kui_document_router_add_route(g_doc, "/tmp", 0, "tmp", 0, make_view, &g_view_kinds[VIEW_HOME], NULL);
    status = kui_document_router_remove_route(g_doc, "/tmp", 0);
    kui_document_router_get_route_count(g_doc, &count);
    check("rt.remove_route", status == KUI_OK && count == 4 &&
          kui_document_router_remove_route(g_doc, "/tmp", 0) == KUI_ERR_NOT_FOUND,
          "status %d, count %u", status, count);

    status = kui_document_router_register_view(g_doc, view);
    kui_document_router_add_navigated_callback(g_doc, on_navigated, NULL, NULL, &g_nav_callback);
    check("rt.register_view", status == KUI_OK && view_children() == 0, "status %d, children %u", status,
          (unsigned)view_children());

    navigated = push("/");
    current_path(path, sizeof path);
    view_id(id, sizeof id);
    check("rt.push", navigated == 1 && strcmp(path, "/") == 0 && strcmp(id, "view-home") == 0,
          "navigated %d, path '%s', view '%s'", navigated, path, id);
    check("rt.navigated_callback", g_nav_calls == 1 && strcmp(g_nav_path, "/") == 0,
          "calls %d, last path '%s'", g_nav_calls, g_nav_path);

    navigated = push("/user/42?tab=2");
    param[0] = '\0';
    kui_document_router_get_param(g_doc, "id", 0, param, sizeof param, &length);
    view_id(id, sizeof id);
    {
        char text[32];
        text_of("view-user", text, sizeof text);
        check("rt.push_param", navigated == 1 && strcmp(param, "42") == 0 && strcmp(id, "view-user") == 0 &&
              strcmp(text, "User 42") == 0, "navigated %d, param id '%s', view '%s' '%s'", navigated, param,
              id, text);
    }
    memset(&route, 0, sizeof route);
    status = kui_document_router_get_current(g_doc, copy_route, &route);
    check("rt.get_current", status == KUI_OK && strcmp(route.path, "/user/42?tab=2") == 0 &&
          strcmp(route.pattern, "/user/:id") == 0 && strcmp(route.query, "tab=2") == 0 &&
          strcmp(route.tab, "2") == 0 && route.params == 1,
          "path '%s', pattern '%s', query '%s', tab '%s', params %u", route.path, route.pattern,
          route.query, route.tab, (unsigned)route.params);
    check("rt.get_param_missing", kui_document_router_get_param(g_doc, "nope", 0, param, sizeof param,
          &length) == KUI_ERR_NOT_FOUND, "get_param(nope) should be NOT_FOUND");
    history(&index, &size);
    check("rt.history", index == 1 && size == 2, "index %d, size %d (want 1, 2)", index, size);

    kui_document_router_go(g_doc, -1, &navigated);
    current_path(path, sizeof path);
    history(&index, &size);
    check("rt.go_back", navigated == 1 && strcmp(path, "/") == 0 && index == 0 && size == 2,
          "navigated %d, path '%s', index %d, size %d", navigated, path, index, size);
    kui_document_router_go(g_doc, 1, &navigated);
    current_path(path, sizeof path);
    check("rt.go_forward", navigated == 1 && strcmp(path, "/user/42?tab=2") == 0, "navigated %d, path '%s'",
          navigated, path);
    kui_document_router_go(g_doc, 5, &navigated);
    check("rt.go_out_of_range", navigated == 0, "go(5) navigated %d", navigated);

    kui_document_router_replace(g_doc, "/about", 0, &navigated);
    current_path(path, sizeof path);
    history(&index, &size);
    view_id(id, sizeof id);
    check("rt.replace", navigated == 1 && strcmp(path, "/about") == 0 && index == 1 && size == 2 &&
          strcmp(id, "view-about") == 0, "navigated %d, path '%s', index %d, size %d, view '%s'",
          navigated, path, index, size, id);

    /* Guards: before-enter refuses /admin; before-leave refuses /about -> /. */
    kui_document_router_add_guard(g_doc, KUI_C_ROUTE_GUARD_BEFORE_ENTER, guard_enter, NULL, NULL, &enter);
    calls = g_nav_calls;
    navigated = push("/admin");
    current_path(path, sizeof path);
    check("rt.guard_enter_blocks", navigated == 0 && strcmp(path, "/about") == 0 && g_nav_calls == calls &&
          strcmp(g_guard_seen, "/about -> /admin") == 0, "navigated %d, path '%s', callbacks %d -> %d, "
          "guard saw '%s'", navigated, path, calls, g_nav_calls, g_guard_seen);

    kui_document_router_add_guard(g_doc, KUI_C_ROUTE_GUARD_BEFORE_LEAVE, guard_leave, NULL, NULL, &leave);
    g_block_leave_about = 1;
    navigated = push("/");
    current_path(path, sizeof path);
    check("rt.guard_leave_blocks", navigated == 0 && strcmp(path, "/about") == 0, "navigated %d, path '%s'",
          navigated, path);
    kui_document_router_remove_guard(g_doc, leave);
    navigated = push("/");
    current_path(path, sizeof path);
    check("rt.remove_guard", navigated == 1 && strcmp(path, "/") == 0, "navigated %d, path '%s'",
          navigated, path);
    kui_document_router_remove_guard(g_doc, enter);
    navigated = push("/admin");
    view_id(id, sizeof id);
    check("rt.guard_removed_allows", navigated == 1 && strcmp(id, "view-admin") == 0,
          "navigated %d, view '%s'", navigated, id);

    /* A path no route matches still navigates and empties the view. */
    navigated = push("/missing");
    memset(&route, 0, sizeof route);
    kui_document_router_get_current(g_doc, copy_route, &route);
    check("rt.unmatched", navigated == 1 && route.pattern[0] == '\0' && view_children() == 0,
          "navigated %d, pattern '%s', children %u", navigated, route.pattern, (unsigned)view_children());

    calls = g_nav_calls;
    status = kui_document_router_remove_navigated_callback(g_doc, g_nav_callback);
    push("/about");
    check("rt.remove_navigated_callback", status == KUI_OK && g_nav_calls == calls,
          "status %d, callbacks %d -> %d", status, calls, g_nav_calls);

    status = kui_document_router_unregister_view(g_doc, view);
    push("/");
    check("rt.unregister_view", status == KUI_OK && view_children() == 1 &&
          kui_document_router_unregister_view(g_doc, view) == KUI_ERR_NOT_FOUND,
          "status %d, children after navigating unregistered %u (want the old 1)", status,
          (unsigned)view_children());
    /* Registering fills the view right away with the current route. */
    kui_document_router_register_view(g_doc, view);
    view_id(id, sizeof id);
    check("rt.register_fills", strcmp(id, "view-home") == 0, "view after register '%s'", id);

    status = kui_document_router_clear_routes(g_doc);
    kui_document_router_get_route_count(g_doc, &count);
    kui_document_router_has_route(g_doc, "/", 0, &has);
    check("rt.clear_routes", status == KUI_OK && count == 0 && has == 0, "status %d, count %u, has / %d",
          status, count, has);

    /* Set up again for the panel: routes, the /admin lock and the navigated callback. */
    add_routes();
    kui_document_router_add_guard(g_doc, KUI_C_ROUTE_GUARD_BEFORE_ENTER, guard_admin, NULL, NULL, &g_admin_guard);
    kui_document_router_add_navigated_callback(g_doc, on_navigated, NULL, NULL, &g_nav_callback);
    /* The cleared "/" route stays referenced while it is the current route's view factory; the
     * next navigation lets go of it and its release_fn runs (once). */
    released = g_route_released;
    push("/");
    check("rt.route_release_fn", released == 0 && g_route_released == 1,
          "release_fn ran %d times before / %d after the next navigation (want 0 / 1)", released,
          g_route_released);
    release(view);
}

/* ---- Resize & intersection observers: entries after rendered frames (timer chain) -------- */

typedef struct {
    int reports;
    float content_w, content_h, border_w, border_h, content_x, content_y;
} SizeLog;

typedef struct {
    int reports;
    int intersecting;
    float ratio;
    KuiRect root;
} SeenLog;

static KuiObserver* g_resize;
static KuiObserver* g_inter;
static KuiObserver* g_viewport;
static SizeLog g_rs_box, g_rs_fluid;
static SeenLog g_io_a, g_io_b, g_io_panel;
static int g_rs_width = 160;

static void show_resize(void) {
    char out[128];
    snprintf(out, sizeof out, "box %.0fx%.0f (border %.0fx%.0f) | fluid %.0f", g_rs_box.content_w,
             g_rs_box.content_h, g_rs_box.border_w, g_rs_box.border_h, g_rs_fluid.content_w);
    kuex_set_text(g_doc, "rs-out", out);
}

static void on_resize(const KuiCResizeEntry* entries, size_t count, void* user_data) {
    size_t index;
    char id[32];
    (void)user_data;
    for (index = 0; index < count; index++) {
        SizeLog* log = NULL;
        id_of(entries[index].target, id, sizeof id);
        if (strcmp(id, "rs-box") == 0) log = &g_rs_box;
        if (strcmp(id, "rs-fluid") == 0) log = &g_rs_fluid;
        if (!log) continue;
        log->reports++;
        log->content_w = entries[index].content_width;
        log->content_h = entries[index].content_height;
        log->border_w = entries[index].border_width;
        log->border_h = entries[index].border_height;
        log->content_x = entries[index].content_x;
        log->content_y = entries[index].content_y;
    }
    show_resize();
}

static void show_intersection(void) {
    char out[96];
    snprintf(out, sizeof out, "A %s %.2f | B %s %.2f", g_io_a.intersecting ? "in" : "out", g_io_a.ratio,
             g_io_b.intersecting ? "in" : "out", g_io_b.ratio);
    kuex_set_text(g_doc, "io-out", out);
}

static void on_intersection(const KuiCIntersectionEntry* entries, size_t count, void* user_data) {
    size_t index;
    char id[32];
    for (index = 0; index < count; index++) {
        SeenLog* log = NULL;
        id_of(entries[index].target, id, sizeof id);
        if (user_data == &g_io_panel) log = &g_io_panel;
        else if (strcmp(id, "io-a") == 0) log = &g_io_a;
        else if (strcmp(id, "io-b") == 0) log = &g_io_b;
        if (!log) continue;
        log->reports++;
        log->intersecting = entries[index].is_intersecting;
        log->ratio = entries[index].intersection_ratio;
        log->root = entries[index].root_bounds;
    }
    if (user_data != &g_io_panel) show_intersection();
}

static void observe_id(KuiObserver* obs, const char* id) {
    KuiElement* element = kuex_by_id(g_doc, id);
    if (element) kui_observer_observe(obs, element);
    release(element);
}

static void unobserve_id(KuiObserver* obs, const char* id) {
    KuiElement* element = kuex_by_id(g_doc, id);
    if (element) kui_observer_unobserve(obs, element);
    release(element);
}

static void set_class(const char* id, const char* name, int on) {
    KuiElement* element = kuex_by_id(g_doc, id);
    if (element && on) kui_element_class_list_add(element, name, 0);
    if (element && !on) kui_element_class_list_remove(element, name, 0);
    release(element);
}

static int g_fluid_reports_frozen;
static float g_fluid_before;

static void frames_phase3(void* user_data) {
    (void)user_data;
    check("rs.unobserve", g_rs_fluid.reports == g_fluid_reports_frozen,
          "unobserved fluid reports %d -> %d", g_fluid_reports_frozen, g_rs_fluid.reports);
    /* The resize observer was retained once and released once in phase 1: still delivering. */
    check("rs.retain_release", near(g_rs_box.content_w, 160, 0.5f),
          "box content width after the reset %.1f (want 160)", g_rs_box.content_w);
    check("io.scroll_back", g_io_a.intersecting && near(g_io_a.ratio, 1, 0.01f) && !g_io_b.intersecting,
          "A %d %.2f, B %d", g_io_a.intersecting, g_io_a.ratio, g_io_b.intersecting);
    kui_observer_release(g_viewport);
    g_viewport = NULL;
    observe_id(g_resize, "rs-fluid"); /* back on for the panel buttons */
    finish();
}

static void frames_phase2(void* user_data) {
    KuiRect fluid = rect_of("rs-fluid");
    KuiTimerId timer;
    (void)user_data;
    check("rs.resize_box", g_rs_box.reports >= 2 && near(g_rs_box.content_w, 200, 0.5f) &&
          near(g_rs_box.border_w, 220, 0.5f), "reports %d, content %.1f, border %.1f (want 200, 220)",
          g_rs_box.reports, g_rs_box.content_w, g_rs_box.border_w);
    check("rs.resize_layout", g_rs_fluid.reports >= 2 && near(g_rs_fluid.content_w, fluid.width, 0.6f) &&
          g_rs_fluid.content_w < g_fluid_before - 10, "reports %d, content %.1f (laid out %.1f, before %.1f)",
          g_rs_fluid.reports, g_rs_fluid.content_w, fluid.width, g_fluid_before);
    check("io.scroll_in", g_io_b.intersecting && near(g_io_b.ratio, 1, 0.01f),
          "B intersecting %d, ratio %.2f", g_io_b.intersecting, g_io_b.ratio);
    check("io.scroll_out", !g_io_a.intersecting && near(g_io_a.ratio, 0, 0.01f),
          "A intersecting %d, ratio %.2f", g_io_a.intersecting, g_io_a.ratio);

    /* Stop watching the fluid box, then change everything back. */
    unobserve_id(g_resize, "rs-fluid");
    g_fluid_reports_frozen = g_rs_fluid.reports;
    set_class("rs-stage", "narrow", 0);
    set_style("rs-box", "width", "160px");
    scroll_to("io-scroller", 0);
    if (kui_platform_set_timeout(frames_phase3, NULL, 350, &timer) != KUI_OK) {
        check("frames.timer", 0, "kui_platform_set_timeout failed");
        finish();
    }
}

static void frames_phase1(void* user_data) {
    KuiRect stage = rect_of("rs-stage");
    float client_x = 0, client_y = 0, client_w = 0, client_h = 0;
    KuiElement* scroller = kuex_by_id(g_doc, "io-scroller");
    KuiTimerId timer;
    (void)user_data;
    if (scroller) kui_element_get_client_metrics(scroller, &client_x, &client_y, &client_w, &client_h);
    release(scroller);

    /* content-box 160x40, padding 8, border 2: border box 180x60, content at 10,10. */
    check("rs.first_report", g_rs_box.reports >= 1 && near(g_rs_box.content_w, 160, 0.5f) &&
          near(g_rs_box.content_h, 40, 0.5f) && near(g_rs_box.border_w, 180, 0.5f) &&
          near(g_rs_box.border_h, 60, 0.5f),
          "reports %d, content %.1fx%.1f, border %.1fx%.1f", g_rs_box.reports,
          g_rs_box.content_w, g_rs_box.content_h, g_rs_box.border_w, g_rs_box.border_h);
    /* kui_c.h: content_x / content_y are the content box offset inside the border box. */
    known("rs.content_offset", near(g_rs_box.content_x, 10, 0.5f) && near(g_rs_box.content_y, 10, 0.5f),
          "content_x / content_y give the padding offset (contentRect.x), not the documented offset "
          "inside the border box (padding + border)",
          "offset %.1f,%.1f (want 10,10)", g_rs_box.content_x, g_rs_box.content_y);
    /* 50% of the stage's content box (border 1 + padding 4 on each side). */
    check("rs.percentage", g_rs_fluid.reports >= 1 && near(g_rs_fluid.content_w, (stage.width - 10) / 2, 0.6f),
          "reports %d, content %.1f, stage %.1f", g_rs_fluid.reports, g_rs_fluid.content_w, stage.width);
    check("io.first_report", g_io_a.reports >= 1 && g_io_b.reports >= 1, "reports A %d, B %d",
          g_io_a.reports, g_io_b.reports);
    check("io.visible", g_io_a.intersecting && near(g_io_a.ratio, 1, 0.01f), "A intersecting %d, ratio %.2f",
          g_io_a.intersecting, g_io_a.ratio);
    check("io.hidden", !g_io_b.intersecting && near(g_io_b.ratio, 0, 0.01f), "B intersecting %d, ratio %.2f",
          g_io_b.intersecting, g_io_b.ratio);
    /* W3C IntersectionObserver (which the native observer follows): an element root that clips
     * its content intersects through its padding area, i.e. the client rect. */
    known("io.root_bounds", near(g_io_a.root.height, client_h, 1.0f) && near(g_io_a.root.width, client_w, 1.0f),
          "root_bounds is the root's border box (border and scrollbar included), not its padding area",
          "root %.1fx%.1f, scroller client %.1fx%.1f", g_io_a.root.width, g_io_a.root.height, client_w,
          client_h);
    check("io.viewport_root", g_io_panel.reports >= 1 && g_io_panel.intersecting,
          "panel against the viewport: reports %d, intersecting %d", g_io_panel.reports, g_io_panel.intersecting);

    kui_observer_release(g_resize); /* drops the extra reference taken in start_frame_checks() */
    g_fluid_before = g_rs_fluid.content_w;
    set_style("rs-box", "width", "200px");
    set_class("rs-stage", "narrow", 1);
    scroll_to("io-scroller", 10000);
    if (kui_platform_set_timeout(frames_phase2, NULL, 350, &timer) != KUI_OK) {
        check("frames.timer", 0, "kui_platform_set_timeout failed");
        finish();
    }
}

static void start_frame_checks(void) {
    static const float thresholds[3] = {0.0f, 0.5f, 1.0f};
    KuiCIntersectionOptions options;
    KuiElement* root = kuex_by_id(g_doc, "io-scroller");
    KuiStatus resize, inter, viewport, retain;
    KuiTimerId timer;

    memset(&options, 0, sizeof options);
    options.root = root;
    options.thresholds = thresholds;
    options.threshold_count = 3;
    resize = kui_resize_observer_create(on_resize, NULL, NULL, &g_resize);
    inter = kui_intersection_observer_create(&options, on_intersection, NULL, NULL, &g_inter);
    viewport = kui_intersection_observer_create(NULL, on_intersection, &g_io_panel, NULL, &g_viewport);
    release(root);
    check("frames.create", resize == KUI_OK && inter == KUI_OK && viewport == KUI_OK,
          "resize %d, intersection %d, viewport intersection %d", resize, inter, viewport);
    if (resize != KUI_OK || inter != KUI_OK || viewport != KUI_OK) {
        finish();
        return;
    }
    retain = kui_observer_retain(g_resize);
    check("frames.retain", retain == KUI_OK, "observer_retain %d", retain);
    observe_id(g_resize, "rs-box");
    observe_id(g_resize, "rs-fluid");
    observe_id(g_inter, "io-a");
    observe_id(g_inter, "io-b");
    observe_id(g_viewport, "p-io");
    if (kui_platform_set_timeout(frames_phase1, NULL, 350, &timer) != KUI_OK) {
        check("frames.timer", 0, "kui_platform_set_timeout failed");
        finish();
    }
}

/* ---- Panel: observable & bindings ---------------------------------------------------------- */

static KuiObservable* g_count;
static KuiObservable* g_level;
static KuiObservable* g_badge_style;
static KuiObservable* g_name;
static uint32_t g_text_binding;

static void show_observable(void) {
    char count[32], text[32], level[32], out[160];
    observable_text(g_count, count, sizeof count);
    observable_text(g_level, level, sizeof level);
    text_of("ob-text", text, sizeof text);
    snprintf(out, sizeof out, "count=%s text=%s level=%s bound=%s", count, text, level,
             g_text_binding ? "yes" : "no");
    kuex_set_text(g_doc, "ob-out", out);
}

static void on_name_changed(void* user_data) {
    char value[96], line[128];
    (void)user_data;
    observable_text(g_name, value, sizeof value);
    snprintf(line, sizeof line, "echo: %s", value);
    kuex_set_text(g_doc, "ob-echo", line);
}

static void setup_observable_panel(void) {
    uint32_t id;
    kui_observable_create("0", 0, &g_count);
    kui_observable_create("cool", 0, &g_level);
    kui_observable_create("background-color: #dbeafe", 0, &g_badge_style);
    kui_observable_create("KUI", 0, &g_name);
    bind_id(g_count, "ob-text", "textContent", 0, &g_text_binding);
    bind_id(g_level, "ob-badge", "data-level", 0, &id);
    bind_id(g_badge_style, "ob-badge", "style", 0, &id);
    bind_id(g_name, "ob-input", "value", 1, &id);
    kui_observable_subscribe(g_name, on_name_changed, NULL, NULL, &id);
    on_name_changed(NULL);
    show_observable();
}

static void on_ob_inc(KuiEvent* event, void* user_data) {
    char value[32];
    (void)event;
    (void)user_data;
    observable_text(g_count, value, sizeof value);
    snprintf(value, sizeof value, "%d", atoi(value) + 1);
    kui_observable_set(g_count, value, 0);
    kui_document_flush_bindings(g_doc);
    show_observable();
}

static void on_ob_color(KuiEvent* event, void* user_data) {
    char level[32];
    int hot;
    (void)event;
    (void)user_data;
    observable_text(g_level, level, sizeof level);
    hot = strcmp(level, "hot") != 0;
    kui_observable_set(g_level, hot ? "hot" : "cool", 0);
    kui_observable_set(g_badge_style, hot ? "background-color: #fee2e2" : "background-color: #dbeafe", 0);
    kui_document_flush_bindings(g_doc);
    show_observable();
}

static void on_ob_unbind(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    if (g_text_binding) kui_observable_unbind(g_count, g_text_binding);
    g_text_binding = 0;
    show_observable();
}

static void on_ob_rebind(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    if (!g_text_binding) bind_id(g_count, "ob-text", "textContent", 0, &g_text_binding);
    show_observable();
}

/* ---- Panel: MutationObserver ---------------------------------------------------------------- */

static KuiObserver* g_mut_panel;
static int g_mut_total;
static int g_mut_next = 1;

static void on_panel_mutations(const KuiCMutationRecord* records, size_t count, void* user_data) {
    static const char* const types[3] = {"childList", "attributes", "characterData"};
    const KuiCMutationRecord* last = &records[count - 1];
    char target[32], added[32], removed[32], out[200];
    size_t used;
    (void)user_data;
    g_mut_total += (int)count;
    id_of(last->target, target, sizeof target);
    id_of(last->added, added, sizeof added);
    id_of(last->removed, removed, sizeof removed);
    snprintf(out, sizeof out, "records=%d | %s #%s", g_mut_total, types[last->type], target);
    used = strlen(out);
    if (added[0]) used += (size_t)snprintf(out + used, sizeof out - used, " +#%s", added);
    if (removed[0]) used += (size_t)snprintf(out + used, sizeof out - used, " -#%s", removed);
    if (last->type == KUI_C_MUTATION_ATTRIBUTES) {
        used += (size_t)snprintf(out + used, sizeof out - used, " %.*s", (int)last->attribute_name_len,
                                 last->attribute_name);
    }
    if (last->type != KUI_C_MUTATION_CHILD_LIST) {
        snprintf(out + used, sizeof out - used, " old=%.*s", (int)last->old_value_len, last->old_value);
    }
    kuex_set_text(g_doc, "mut-out", out);
}

static void observe_mut_panel(void) {
    KuiCMutationOptions options;
    KuiElement* box = kuex_by_id(g_doc, "mut-box");
    memset(&options, 0, sizeof options);
    options.child_list = 1;
    options.attributes = 1;
    options.character_data = 1;
    options.subtree = 1;
    options.attribute_old_value = 1;
    options.character_data_old_value = 1;
    if (box) kui_mutation_observer_observe(g_mut_panel, box, &options);
    release(box);
}

static void on_mut_add(KuiEvent* event, void* user_data) {
    char id[32], text[32];
    KuiElement* list = kuex_by_id(g_doc, "mut-list");
    KuiElement* item;
    (void)event;
    (void)user_data;
    snprintf(id, sizeof id, "mut-li-%d", g_mut_next);
    snprintf(text, sizeof text, "item %d", g_mut_next);
    g_mut_next++;
    item = kuex_make_element(g_doc, "li", text, NULL);
    if (item) kui_element_set_attribute(item, "id", 0, id, 0);
    kuex_append_release(list, item);
    release(list);
}

static void on_mut_remove(KuiEvent* event, void* user_data) {
    KuiElement* list = kuex_by_id(g_doc, "mut-list");
    KuiElement* last = NULL;
    (void)event;
    (void)user_data;
    if (list && kui_element_last_child(list, &last) == KUI_OK && last) {
        kui_element_remove_child(list, last);
        kui_element_release(last);
    }
    release(list);
}

static void on_mut_attr(KuiEvent* event, void* user_data) {
    char state[16];
    (void)event;
    (void)user_data;
    attr_of("mut-item", "data-state", state, sizeof state);
    set_attr("mut-item", "data-state", strcmp(state, "on") == 0 ? "off" : "on");
}

static void on_mut_text(KuiEvent* event, void* user_data) {
    char text[32];
    (void)event;
    (void)user_data;
    text_of("mut-text", text, sizeof text);
    kuex_set_text(g_doc, "mut-text", strcmp(text, "hello") == 0 ? "world" : "hello");
}

static void on_mut_stop(KuiEvent* event, void* user_data) {
    char out[64];
    (void)event;
    (void)user_data;
    kui_observer_disconnect(g_mut_panel);
    snprintf(out, sizeof out, "disconnected (records=%d)", g_mut_total);
    kuex_set_text(g_doc, "mut-out", out);
}

static void on_mut_start(KuiEvent* event, void* user_data) {
    char out[64];
    (void)event;
    (void)user_data;
    observe_mut_panel();
    snprintf(out, sizeof out, "observing (records=%d)", g_mut_total);
    kuex_set_text(g_doc, "mut-out", out);
}

/* ---- Panel: resize & intersection buttons ------------------------------------------------- */

static void apply_rs_width(void) {
    char value[16];
    snprintf(value, sizeof value, "%dpx", g_rs_width);
    set_style("rs-box", "width", value);
}

static void on_rs_grow(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    g_rs_width += 40;
    apply_rs_width();
}

static void on_rs_reset(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    g_rs_width = 160;
    apply_rs_width();
    set_class("rs-stage", "narrow", 0);
}

static void on_rs_stage(KuiEvent* event, void* user_data) {
    int narrow = 0;
    KuiElement* stage = kuex_by_id(g_doc, "rs-stage");
    (void)event;
    (void)user_data;
    if (stage) kui_element_class_list_contains(stage, "narrow", 0, &narrow);
    release(stage);
    set_class("rs-stage", "narrow", !narrow);
}

static void on_io_bottom(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    scroll_to("io-scroller", 10000);
}

static void on_io_top(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    scroll_to("io-scroller", 0);
}

/* ---- Panel: themes ------------------------------------------------------------------------- */

static void show_theme(void) {
    char active[64], bg[32], out[128];
    active_theme(active, sizeof active);
    html_custom_property("--tour-bg", bg, sizeof bg);
    snprintf(out, sizeof out, "active=%s | --tour-bg=%s", active, bg);
    kuex_set_text(g_doc, "theme-out", out);
}

static void on_th_light(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    kui_theme_manager_apply(g_themes, "tour-light", 0, g_doc);
    show_theme();
}

static void on_th_dark(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    kui_theme_manager_apply(g_themes, "tour-dark", 0, g_doc);
    show_theme();
}

static void on_th_list(KuiEvent* event, void* user_data) {
    size_t count = 0, index, length, vars;
    char name[64], out[256];
    size_t used;
    (void)event;
    (void)user_data;
    kui_theme_manager_get_theme_count(g_themes, &count);
    used = (size_t)snprintf(out, sizeof out, "%u themes:", (unsigned)count);
    for (index = 0; index < count && used < sizeof out; index++) {
        length = 0;
        vars = 0;
        kui_theme_manager_get_theme_name(g_themes, index, name, sizeof name, &length);
        kui_theme_manager_get_variable_count(g_themes, name, 0, &vars);
        used += (size_t)snprintf(out + used, sizeof out - used, " %s(%u)", name, (unsigned)vars);
    }
    kuex_set_text(g_doc, "theme-out", out);
}

/* ---- Panel: router ------------------------------------------------------------------------- */

static void show_route(const char* note) {
    RouteCopy route;
    char out[200], id[32];
    size_t length = 0;
    int index, size;
    memset(&route, 0, sizeof route);
    kui_document_router_get_current(g_doc, copy_route, &route);
    history(&index, &size);
    id[0] = '\0';
    if (kui_document_router_get_param(g_doc, "id", 0, id, sizeof id, &length) != KUI_OK) {
        snprintf(id, sizeof id, "-");
    }
    snprintf(out, sizeof out, "%spath=%s pattern=%s | %d/%d | id=%s | locked=%s", note, route.path,
             route.pattern, index, size, id, g_admin_locked ? "yes" : "no");
    kuex_set_text(g_doc, "route-out", out);
}

static void navigate(const char* path, int replace) {
    int navigated = 0;
    char note[96];
    if (replace) kui_document_router_replace(g_doc, path, 0, &navigated);
    else kui_document_router_push(g_doc, path, 0, &navigated);
    snprintf(note, sizeof note, "blocked %s | ", path);
    show_route(navigated ? "" : note);
}

static void on_rt_home(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    navigate("/", 0);
}

static void on_rt_user(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    navigate("/user/7", 0);
}

static void on_rt_about(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    navigate("/about", 1);
}

static void on_rt_admin(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    navigate("/admin", 0);
}

static void on_rt_lock(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    g_admin_locked = !g_admin_locked;
    show_route("");
}

static void on_rt_back(KuiEvent* event, void* user_data) {
    int navigated = 0;
    (void)event;
    (void)user_data;
    kui_document_router_go(g_doc, -1, &navigated);
    show_route(navigated ? "" : "no back | ");
}

static void on_rt_fwd(KuiEvent* event, void* user_data) {
    int navigated = 0;
    (void)event;
    (void)user_data;
    kui_document_router_go(g_doc, 1, &navigated);
    show_route(navigated ? "" : "no forward | ");
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    int failed = 0;
    g_doc = host->document;

    check_observable();
    check_mutation();
    check_theme();
    check_router();
    start_frame_checks(); /* finishes the verdict from its timers */

    setup_observable_panel();
    kui_mutation_observer_create(on_panel_mutations, NULL, NULL, &g_mut_panel);
    if (g_mut_panel) observe_mut_panel();
    show_theme();
    show_route("");

    failed |= kuex_on(g_doc, "btn-ob-inc", "click", on_ob_inc, NULL);
    failed |= kuex_on(g_doc, "btn-ob-color", "click", on_ob_color, NULL);
    failed |= kuex_on(g_doc, "btn-ob-unbind", "click", on_ob_unbind, NULL);
    failed |= kuex_on(g_doc, "btn-ob-rebind", "click", on_ob_rebind, NULL);
    failed |= kuex_on(g_doc, "btn-mut-add", "click", on_mut_add, NULL);
    failed |= kuex_on(g_doc, "btn-mut-remove", "click", on_mut_remove, NULL);
    failed |= kuex_on(g_doc, "btn-mut-attr", "click", on_mut_attr, NULL);
    failed |= kuex_on(g_doc, "btn-mut-text", "click", on_mut_text, NULL);
    failed |= kuex_on(g_doc, "btn-mut-stop", "click", on_mut_stop, NULL);
    failed |= kuex_on(g_doc, "btn-mut-start", "click", on_mut_start, NULL);
    failed |= kuex_on(g_doc, "btn-rs-grow", "click", on_rs_grow, NULL);
    failed |= kuex_on(g_doc, "btn-rs-reset", "click", on_rs_reset, NULL);
    failed |= kuex_on(g_doc, "btn-rs-stage", "click", on_rs_stage, NULL);
    failed |= kuex_on(g_doc, "btn-io-bottom", "click", on_io_bottom, NULL);
    failed |= kuex_on(g_doc, "btn-io-top", "click", on_io_top, NULL);
    failed |= kuex_on(g_doc, "btn-th-light", "click", on_th_light, NULL);
    failed |= kuex_on(g_doc, "btn-th-dark", "click", on_th_dark, NULL);
    failed |= kuex_on(g_doc, "btn-th-list", "click", on_th_list, NULL);
    failed |= kuex_on(g_doc, "btn-rt-home", "click", on_rt_home, NULL);
    failed |= kuex_on(g_doc, "btn-rt-user", "click", on_rt_user, NULL);
    failed |= kuex_on(g_doc, "btn-rt-about", "click", on_rt_about, NULL);
    failed |= kuex_on(g_doc, "btn-rt-admin", "click", on_rt_admin, NULL);
    failed |= kuex_on(g_doc, "btn-rt-lock", "click", on_rt_lock, NULL);
    failed |= kuex_on(g_doc, "btn-rt-back", "click", on_rt_back, NULL);
    failed |= kuex_on(g_doc, "btn-rt-fwd", "click", on_rt_fwd, NULL);
    return failed;
}
