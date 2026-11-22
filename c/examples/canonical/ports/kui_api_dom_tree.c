/* API tour: host-side DOM tree operations (examples/api_tours/dom_tree). On start the host calls
 * every lookup / traversal / tree-editing / HTML function once, checks the result against what
 * kui_c.h documents and logs one row per check into #tour-log; #tour-result ends as "PASS n/n".
 * The panel buttons repeat the calls on the visible sandboxes. */
#include "kui_c_example_dom.h"

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

/* ---- Small readers (every returned element is a counted reference) ---------------------- */

static void release(KuiElement* element) {
    if (element) kui_element_release(element);
}

static KuiElement* qs(const char* selector) {
    KuiElement* element = NULL;
    if (kui_document_query_selector(g_doc, selector, 0, &element) != KUI_OK) return NULL;
    return element;
}

static void id_of(KuiElement* element, char* buf, size_t capacity) {
    size_t length = 0;
    buf[0] = '\0';
    if (element) kui_element_get_id(element, buf, capacity, &length);
}

static void tag_of(KuiElement* element, char* buf, size_t capacity) {
    size_t length = 0;
    buf[0] = '\0';
    if (element) kui_element_get_tag_name(element, buf, capacity, &length);
}

static int has_id(KuiElement* element, const char* id) {
    char buf[64];
    id_of(element, buf, sizeof buf);
    return element && strcmp(buf, id) == 0;
}

static void inner_html_of(KuiElement* element, char* buf, size_t capacity) {
    size_t length = 0;
    buf[0] = '\0';
    if (element) kui_element_get_inner_html(element, buf, capacity, &length);
}

static size_t children_of(KuiElement* element) {
    size_t count = 0;
    if (element) kui_element_child_count(element, &count);
    return count;
}

/* Joins the texts of `count` handles with ',' and releases them. */
static void join_texts(KuiElement** items, size_t count, char* out, size_t capacity) {
    size_t index;
    size_t used;
    char text[64];
    out[0] = '\0';
    for (index = 0; index < count; ++index) {
        kuex_element_text(items[index], text, sizeof text);
        used = strlen(out);
        snprintf(out + used, capacity - used, "%s%s", index ? "," : "", text);
        kui_element_release(items[index]);
    }
}

/* ---- Document lookups: root / head / body, id, selectors, class and tag lists, hit test ---- */

static void check_lookups(void) {
    KuiElement* root = NULL;
    KuiElement* head = NULL;
    KuiElement* body = NULL;
    KuiElement* parent = NULL;
    KuiElement* found = NULL;
    KuiElement* items[8];
    KuiStatus status;
    size_t count = 0;
    char a[128];
    char b[128];
    KuiRect rect;

    kui_document_root(g_doc, &root);
    tag_of(root, a, sizeof a);
    check("doc.root", strcmp(a, "html") == 0, "root tag '%s'", a);

    kui_document_head(g_doc, &head);
    tag_of(head, a, sizeof a);
    if (head) kui_element_query_selector(head, "title", 0, &found);
    kuex_element_text(found, b, sizeof b);
    check("doc.head", strcmp(a, "head") == 0 && strcmp(b, "KUI API \xe2\x80\x94 DOM tree") == 0,
          "head tag '%s', <title> '%s'", a, b);
    release(found);
    found = NULL;

    kui_document_body(g_doc, &body);
    if (body) kui_element_parent(body, &parent);
    tag_of(parent, a, sizeof a);
    check("doc.body", body && strcmp(a, "html") == 0, "body parent tag '%s'", a);
    release(parent);
    parent = NULL;

    found = kuex_by_id(g_doc, "lookup-box");
    id_of(found, a, sizeof a);
    check("doc.get_element_by_id", strcmp(a, "lookup-box") == 0, "id '%s'", a);
    release(found);
    found = NULL;
    status = kui_document_get_element_by_id(g_doc, "no-such-id", 0, &found);
    check("doc.get_element_by_id-missing", status == KUI_ERR_NOT_FOUND && !found, "status %d", status);

    found = qs("#lookup-box .hot");
    kuex_element_text(found, a, sizeof a);
    check("doc.query_selector", strcmp(a, "beta") == 0, "first .hot text '%s'", a);
    release(found);
    found = NULL;

    /* Two-call protocol: NULL buffer asks for the count, the second call fills handles. */
    kui_document_query_selector_all(g_doc, ".chip", 0, NULL, 0, &count);
    status = kui_document_query_selector_all(g_doc, ".chip", 0, items, 8, &count);
    join_texts(items, status == KUI_OK ? count : 0, a, sizeof a);
    check("doc.query_selector_all", count == 4 && strcmp(a, "alpha,beta,gamma,delta") == 0,
          "count %zu texts '%s'", count, a);

    status = kui_document_get_elements_by_class_name(g_doc, "hot", 0, items, 8, &count);
    join_texts(items, status == KUI_OK ? count : 0, a, sizeof a);
    check("doc.get_elements_by_class_name", count == 2 && strcmp(a, "beta,delta") == 0,
          "count %zu texts '%s'", count, a);
    /* kui_c.h: several space-separated classes must all match. */
    status = kui_document_get_elements_by_class_name(g_doc, "chip hot", 0, items, 8, &count);
    join_texts(items, status == KUI_OK ? count : 0, a, sizeof a);
    known("doc.get_elements_by_class_name-multi", count == 2 && strcmp(a, "beta,delta") == 0,
          "a multi-class query matches nothing", "count %zu texts '%s'", count, a);

    status = kui_document_get_elements_by_tag_name(g_doc, "em", 0, items, 8, &count);
    join_texts(items, status == KUI_OK ? count : 0, a, sizeof a);
    check("doc.get_elements_by_tag_name", count == 1 && strcmp(a, "delta") == 0,
          "count %zu texts '%s'", count, a);

    found = kuex_by_id(g_doc, "lookup-box");
    {
        KuiElement* em = NULL;
        if (found) kui_element_query_selector(found, "em", 0, &em);
        kuex_element_text(em, a, sizeof a);
        check("el.query_selector", strcmp(a, "delta") == 0, "em text '%s'", a);
        release(em);
        count = 0;
        if (found) {
            status = kui_element_query_selector_all(found, "span", 0, items, 8, &count);
            join_texts(items, status == KUI_OK ? count : 0, a, sizeof a);
        }
        check("el.query_selector_all", count == 3 && strcmp(a, "alpha,beta,gamma") == 0,
              "count %zu texts '%s'", count, a);
    }
    release(found);
    found = NULL;

    /* Hit test the centre of #hit-target in viewport coordinates. */
    {
        KuiElement* target = kuex_by_id(g_doc, "hit-target");
        memset(&rect, 0, sizeof rect);
        if (target) kui_element_get_bounding_rect(target, &rect);
        kui_document_element_from_point(g_doc, rect.x + rect.width / 2, rect.y + rect.height / 2,
                                        &found);
        id_of(found, a, sizeof a);
        check("doc.element_from_point", has_id(found, "hit-target"),
              "rect %.0f,%.0f %.0fx%.0f hit '%s'", rect.x, rect.y, rect.width, rect.height, a);
        release(found);
        release(target);
        found = NULL;
    }

    /* An extra document reference is balanced by a release; the document stays usable. */
    status = kui_document_retain(g_doc);
    if (status == KUI_OK) status = kui_document_release(g_doc);
    found = kuex_by_id(g_doc, "lookup-box");
    check("doc.retain", status == KUI_OK && found, "status %d", status);
    release(found);

    release(root);
    release(head);
    release(body);
}

/* ---- Traversal: parent, siblings, first / last child, closest and matches --------------- */

static void check_traversal(void) {
    KuiElement* t2 = kuex_by_id(g_doc, "t2");
    KuiElement* t4 = kuex_by_id(g_doc, "t4");
    KuiElement* list = kuex_by_id(g_doc, "trav-list");
    KuiElement* got = NULL;
    KuiElement* last = NULL;
    KuiStatus status;
    int yes = -1;
    char a[64];
    char b[64];

    if (t2) kui_element_parent(t2, &got);
    id_of(got, a, sizeof a);
    check("el.parent", has_id(got, "trav-list"), "parent '%s'", a);
    release(got);
    got = NULL;

    if (t2) kui_element_next_sibling(t2, &got);
    id_of(got, a, sizeof a);
    check("el.next_sibling", has_id(got, "t3"), "next '%s'", a);
    release(got);
    got = NULL;

    if (t2) kui_element_previous_sibling(t2, &got);
    id_of(got, a, sizeof a);
    check("el.previous_sibling", has_id(got, "t1"), "previous '%s'", a);
    release(got);
    got = NULL;

    status = t4 ? kui_element_next_sibling(t4, &got) : KUI_ERR_INVALID_ARG;
    check("el.next_sibling-end", status == KUI_ERR_NOT_FOUND && !got, "status %d", status);
    release(got);
    got = NULL;

    if (list) {
        kui_element_first_child(list, &got);
        kui_element_last_child(list, &last);
    }
    id_of(got, a, sizeof a);
    id_of(last, b, sizeof b);
    check("el.first_last_child", has_id(got, "t1") && has_id(last, "t4") && children_of(list) == 4,
          "first '%s' last '%s' count %zu", a, b, children_of(list));
    release(got);
    release(last);
    got = NULL;

    if (t2) kui_element_closest(t2, "ul", 0, &got);
    id_of(got, a, sizeof a);
    check("el.closest-ancestor", has_id(got, "trav-list"), "closest(ul) '%s'", a);
    release(got);
    got = NULL;

    if (t2) kui_element_closest(t2, "li", 0, &got);
    id_of(got, a, sizeof a);
    check("el.closest-inclusive", has_id(got, "t2"), "closest(li) '%s'", a);
    release(got);
    got = NULL;

    status = t2 ? kui_element_closest(t2, "table", 0, &got) : KUI_ERR_INVALID_ARG;
    check("el.closest-none", status == KUI_ERR_NOT_FOUND && !got, "status %d", status);
    release(got);
    got = NULL;

    if (t2) kui_element_matches(t2, "li.mid", 0, &yes);
    check("el.matches", yes == 1, "matches(li.mid) = %d", yes);
    yes = -1;
    if (t2) kui_element_matches(t2, "#t1", 0, &yes);
    check("el.matches-false", yes == 0, "matches(#t1) = %d", yes);

    release(t2);
    release(t4);
    release(list);
}

/* ---- Create & insert (in the hidden #scratch): create, append, insert_before, clone, replace -- */

static void check_create_insert(void) {
    KuiElement* scratch = kuex_by_id(g_doc, "scratch");
    KuiElement* a = NULL;
    KuiElement* first = NULL;
    KuiElement* src = NULL;
    KuiElement* deep = NULL;
    KuiElement* shallow = NULL;
    KuiElement* got = NULL;
    KuiStatus status;
    char buf[128];
    char tag[32];

    kui_document_create_element(g_doc, "div", 0, &a);
    if (a) {
        kui_element_set_attribute(a, "id", 0, "sc-a", 0);
        kui_element_set_attribute(a, "data-k", 0, "v", 0);
        if (scratch) kui_element_append_child(scratch, a);
    }
    got = kuex_by_id(g_doc, "sc-a");
    kuex_get_attr(g_doc, "sc-a", "data-k", buf, sizeof buf);
    check("el.create_append", children_of(scratch) == 1 && got && strcmp(buf, "v") == 0,
          "children %zu, found %d, data-k '%s'", children_of(scratch), got != NULL, buf);
    release(got);
    got = NULL;

    kui_document_create_element(g_doc, "p", 0, &first);
    if (first) kui_element_set_attribute(first, "id", 0, "sc-first", 0);
    status = (scratch && first) ? kui_element_insert_before(scratch, first, a) : KUI_ERR_INVALID_ARG;
    if (scratch) kui_element_first_child(scratch, &got);
    id_of(got, buf, sizeof buf);
    check("el.insert_before", status == KUI_OK && has_id(got, "sc-first"),
          "status %d, first child '%s'", status, buf);
    release(got);
    got = NULL;

    kui_document_create_element(g_doc, "div", 0, &src);
    if (src) {
        kui_element_set_attribute(src, "id", 0, "sc-src", 0);
        kui_element_set_inner_html(src, "<b>bold</b> tail", 0);
        if (scratch) kui_element_append_child(scratch, src);
        kui_element_clone(src, 1, &deep);
        kui_element_clone(src, 0, &shallow);
    }
    {
        KuiElement* bold = NULL;
        KuiStatus parent_status = deep ? kui_element_parent(deep, &got) : KUI_ERR_INVALID_ARG;
        if (deep) kui_element_query_selector(deep, "b", 0, &bold);
        kuex_element_text(deep, buf, sizeof buf);
        check("el.clone-deep",
              strcmp(buf, "bold tail") == 0 && parent_status == KUI_ERR_NOT_FOUND && bold,
              "text '%s', parent status %d, <b> %d", buf, parent_status, bold != NULL);
        release(bold);
        release(got);
        got = NULL;
    }
    kuex_element_text(shallow, buf, sizeof buf);
    tag_of(shallow, tag, sizeof tag);
    check("el.clone-shallow", shallow && buf[0] == '\0' && children_of(shallow) == 0 &&
          strcmp(tag, "div") == 0, "text '%s', children %zu, tag '%s'", buf,
          children_of(shallow), tag);

    /* The deep copy (renamed, ids stay unique) takes #sc-a's place. */
    if (deep) kui_element_set_attribute(deep, "id", 0, "sc-copy", 0);
    status = (scratch && deep && a) ? kui_element_replace_child(scratch, deep, a) : KUI_ERR_INVALID_ARG;
    {
        KuiElement* old_parent = NULL;
        KuiElement* new_parent = NULL;
        KuiElement* lookup = kuex_by_id(g_doc, "sc-a");
        if (a) kui_element_parent(a, &old_parent);
        if (deep) kui_element_parent(deep, &new_parent);
        check("el.replace_child", status == KUI_OK && !old_parent && !lookup &&
              has_id(new_parent, "scratch"), "status %d, old parent %d, lookup %d", status,
              old_parent != NULL, lookup != NULL);
        release(old_parent);
        release(new_parent);
        release(lookup);
    }

    release(scratch);
    release(a);
    release(first);
    release(src);
    release(deep);
    release(shallow);
}

/* ---- Remove, attributes and handle references (still in #scratch) ---------------------- */

static void check_remove_attributes(void) {
    KuiElement* scratch = kuex_by_id(g_doc, "scratch");
    KuiElement* first = kuex_by_id(g_doc, "sc-first");
    KuiElement* src = kuex_by_id(g_doc, "sc-src");
    KuiElement* copy = kuex_by_id(g_doc, "sc-copy");
    KuiElement* got = NULL;
    KuiStatus status;
    size_t before;
    int has = -1;
    char buf[64];
    size_t length = 0;

    /* A removed element leaves the document, but our handle keeps it alive and readable. */
    status = first ? kui_element_remove(first) : KUI_ERR_INVALID_ARG;
    got = kuex_by_id(g_doc, "sc-first");
    tag_of(first, buf, sizeof buf);
    check("el.remove", status == KUI_OK && !got && strcmp(buf, "p") == 0,
          "status %d, still found %d, handle tag '%s'", status, got != NULL, buf);
    release(got);
    got = NULL;

    before = children_of(scratch);
    status = (scratch && src) ? kui_element_remove_child(scratch, src) : KUI_ERR_INVALID_ARG;
    if (src) kui_element_parent(src, &got);
    check("el.remove_child", status == KUI_OK && children_of(scratch) + 1 == before && !got,
          "status %d, children %zu -> %zu", status, before, children_of(scratch));
    release(got);
    got = NULL;

    if (copy) kui_element_set_attribute(copy, "title", 0, "hello", 0);
    kuex_get_attr(g_doc, "sc-copy", "title", buf, sizeof buf);
    check("el.set_get_attribute", strcmp(buf, "hello") == 0, "title '%s'", buf);
    if (copy) kui_element_has_attribute(copy, "title", 0, &has);
    check("el.has_attribute", has == 1, "has_attribute = %d", has);
    status = copy ? kui_element_remove_attribute(copy, "title", 0) : KUI_ERR_INVALID_ARG;
    has = -1;
    if (copy) kui_element_has_attribute(copy, "title", 0, &has);
    {
        KuiStatus read = copy ? kui_element_get_attribute(copy, "title", 0, buf, sizeof buf, &length)
                              : KUI_ERR_INVALID_ARG;
        check("el.remove_attribute", status == KUI_OK && has == 0 && read == KUI_ERR_NOT_FOUND,
              "status %d, has %d, get status %d", status, has, read);
    }

    /* retain adds a reference; after one release the handle is still ours. */
    status = copy ? kui_element_retain(copy) : KUI_ERR_INVALID_ARG;
    if (status == KUI_OK) kui_element_release(copy);
    id_of(copy, buf, sizeof buf);
    check("el.retain", status == KUI_OK && strcmp(buf, "sc-copy") == 0, "status %d, id '%s'",
          status, buf);

    release(scratch);
    release(first);
    release(src);
    release(copy);
}

/* ---- HTML & text: inner / outer HTML, insert_adjacent_html, text nodes ------------------ */

/* ---- Form control state: the current value / checked / selected index, not the content attribute -- */

static void check_form_state(void) {
    KuiElement* scratch = kuex_by_id(g_doc, "scratch");
    KuiElement* box = NULL;
    KuiElement* input;
    KuiElement* select;
    KuiElement* checkbox;
    KuiElement* plain;
    char initial[64];
    char current[64];
    char attribute[64];
    char selected_value[64];
    int32_t before = -9;
    int32_t after = -9;
    int unchecked = -1;
    int now = -1;

    kui_document_create_element(g_doc, "div", 0, &box);
    if (box && scratch) kui_element_append_child(scratch, box);
    if (box) kui_element_set_inner_html(box, "<input id=\"sc-fv\" type=\"text\" value=\"initial\"><select id=\"sc-fs\"><option value=\"s\">S</option><option value=\"m\" selected>M</option><option value=\"l\">L</option></select><input id=\"sc-fc\" type=\"checkbox\"><i id=\"sc-fi\">x</i>", 0);
    input = kuex_by_id(g_doc, "sc-fv");
    select = kuex_by_id(g_doc, "sc-fs");
    checkbox = kuex_by_id(g_doc, "sc-fc");
    plain = kuex_by_id(g_doc, "sc-fi");
    if (!input || !select || !checkbox || !plain) {
        check("form.setup", 0, "form fixture missing");
    } else {
        initial[0] = current[0] = attribute[0] = selected_value[0] = '\0';
        kui_element_get_value(input, initial, sizeof initial, NULL);
        kui_element_set_value(input, "Ada", 0);
        kui_element_get_value(input, current, sizeof current, NULL);
        kui_element_get_attribute(input, "value", 0, attribute, sizeof attribute, NULL);
        check("el.set_get_value", strcmp(initial, "initial") == 0 && strcmp(current, "Ada") == 0 &&
              strcmp(attribute, "initial") == 0,
              "initial '%s', current '%s', attribute '%s'", initial, current, attribute);

        kui_element_get_selected_index(select, &before);
        kui_element_set_selected_index(select, 2);
        kui_element_get_selected_index(select, &after);
        kui_element_get_value(select, selected_value, sizeof selected_value, NULL);
        check("el.set_get_selected_index", before == 1 && after == 2 && strcmp(selected_value, "l") == 0,
              "before %d, after %d, value '%s'", (int)before, (int)after, selected_value);

        kui_element_get_checked(checkbox, &unchecked);
        kui_element_set_checked(checkbox, 1);
        kui_element_get_checked(checkbox, &now);
        check("el.set_get_checked", unchecked == 0 && now == 1, "unchecked first %d, now %d", unchecked, now);

        /* An element with no form state: get_value / get_selected_index are not supported. */
        check("el.form-not-supported",
              kui_element_get_value(plain, current, sizeof current, NULL) == KUI_ERR_NOT_SUPPORTED &&
              kui_element_get_selected_index(plain, &after) == KUI_ERR_NOT_SUPPORTED,
              "status of get_value / get_selected_index on <i>");
    }
    if (input) kui_element_release(input);
    if (select) kui_element_release(select);
    if (checkbox) kui_element_release(checkbox);
    if (plain) kui_element_release(plain);
    if (box) kui_element_release(box);
    if (scratch) kui_element_release(scratch);
}

static void check_html_text(void) {
    KuiElement* scratch = kuex_by_id(g_doc, "scratch");
    KuiElement* box = NULL;
    KuiElement* inner = NULL;
    KuiElement* span = NULL;
    char buf[256];
    char text[64];
    size_t length = 0;

    kui_document_create_element(g_doc, "div", 0, &box);
    if (box && scratch) kui_element_append_child(scratch, box);

    if (box) kui_element_set_inner_html(box, "<i>a</i><i>b</i>", 0);
    check("el.set_inner_html", children_of(box) == 2, "children %zu", children_of(box));
    inner_html_of(box, buf, sizeof buf);
    check("el.get_inner_html", strcmp(buf, "<i>a</i><i>b</i>") == 0, "innerHTML '%s'", buf);

    if (box) kui_element_set_inner_html(box, "<p id=\"sc-o\" class=\"x\">hi</p>", 0);
    inner = kuex_by_id(g_doc, "sc-o");
    buf[0] = '\0';
    if (inner) kui_element_get_outer_html(inner, buf, sizeof buf, &length);
    length = strlen(buf);
    check("el.get_outer_html", strncmp(buf, "<p", 2) == 0 && strstr(buf, "id=\"sc-o\"") &&
          strstr(buf, "class=\"x\"") && length >= 6 && strcmp(buf + length - 6, "hi</p>") == 0,
          "outerHTML '%s'", buf);
    release(inner);
    inner = NULL;

    if (box) kui_element_set_inner_html(box, "<span id=\"sc-t\">T</span>", 0);
    inner = kuex_by_id(g_doc, "sc-t");
    if (inner) {
        kui_element_insert_adjacent_html(inner, "beforebegin", 0, "<i>1</i>", 0);
        kui_element_insert_adjacent_html(inner, "afterbegin", 0, "<i>2</i>", 0);
        kui_element_insert_adjacent_html(inner, "beforeend", 0, "<i>3</i>", 0);
        kui_element_insert_adjacent_html(inner, "afterend", 0, "<i>4</i>", 0);
    }
    kuex_element_text(box, buf, sizeof buf);
    kuex_element_text(inner, text, sizeof text);
    check("el.insert_adjacent_html", strcmp(buf, "12T34") == 0 && strcmp(text, "2T3") == 0 &&
          children_of(box) == 3, "box text '%s', target text '%s', box children %zu", buf, text,
          children_of(box));
    release(inner);

    kui_document_create_element(g_doc, "span", 0, &span);
    if (span) {
        kui_element_set_text_content(span, "a", 0);
        kui_element_append_text(span, "b", 0);
    }
    kuex_element_text(span, text, sizeof text);
    inner_html_of(span, buf, sizeof buf);
    check("el.append_text", strcmp(text, "ab") == 0 && strcmp(buf, "ab") == 0 &&
          children_of(span) == 0, "text '%s', innerHTML '%s'", text, buf);

    /* set_text_content never parses markup: it becomes one escaped text node. */
    if (span) kui_element_set_text_content(span, "<b>x</b>", 0);
    kuex_element_text(span, text, sizeof text);
    inner_html_of(span, buf, sizeof buf);
    check("el.set_text_content", strcmp(text, "<b>x</b>") == 0 && children_of(span) == 0,
          "text '%s', children %zu", text, children_of(span));
    /* HTML serialization escapes '<' '>' '&' in text, so innerHTML parses back to the same text. */
    check("el.get_inner_html-escapes-text", strcmp(buf, "&lt;b&gt;x&lt;/b&gt;") == 0,
          "innerHTML '%s'", buf);

    release(span);
    release(box);
    release(scratch);
}

/* ---- Panel buttons ----------------------------------------------------------------------- */

static int g_build_serial;

static void on_lookup_count(KuiEvent* event, void* user_data) {
    size_t chips = 0, hot = 0, em = 0;
    char out[96];
    (void)event;
    (void)user_data;
    kui_document_query_selector_all(g_doc, ".chip", 0, NULL, 0, &chips);
    kui_document_get_elements_by_class_name(g_doc, "hot", 0, NULL, 0, &hot);
    kui_document_get_elements_by_tag_name(g_doc, "em", 0, NULL, 0, &em);
    snprintf(out, sizeof out, "chips=%zu | hot=%zu | em=%zu", chips, hot, em);
    kuex_set_text(g_doc, "lookup-out", out);
}

static void on_lookup_hit(KuiEvent* event, void* user_data) {
    KuiElement* target = kuex_by_id(g_doc, "hit-target");
    KuiElement* hit = NULL;
    KuiRect rect;
    char id[64];
    char out[96];
    (void)event;
    (void)user_data;
    memset(&rect, 0, sizeof rect);
    if (target) kui_element_get_bounding_rect(target, &rect);
    kui_document_element_from_point(g_doc, rect.x + rect.width / 2, rect.y + rect.height / 2, &hit);
    id_of(hit, id, sizeof id);
    snprintf(out, sizeof out, "element_from_point -> #%s", hit ? id : "(none)");
    kuex_set_text(g_doc, "lookup-out", out);
    release(hit);
    release(target);
}

static void on_lookup_root(KuiEvent* event, void* user_data) {
    KuiElement* root = NULL;
    KuiElement* head = NULL;
    KuiElement* body = NULL;
    char a[32], b[32], c[32], out[128];
    (void)event;
    (void)user_data;
    kui_document_root(g_doc, &root);
    kui_document_head(g_doc, &head);
    kui_document_body(g_doc, &body);
    tag_of(root, a, sizeof a);
    tag_of(head, b, sizeof b);
    tag_of(body, c, sizeof c);
    snprintf(out, sizeof out, "root=%s | head=%s | body=%s", a, b, c);
    kuex_set_text(g_doc, "lookup-out", out);
    release(root);
    release(head);
    release(body);
}

/* Moves the "cur" highlight to the next / previous item, wrapping at the ends. */
static void move_highlight(int forward) {
    KuiElement* current = qs("#trav-list li.cur");
    KuiElement* list = kuex_by_id(g_doc, "trav-list");
    KuiElement* target = NULL;
    char id[64], out[80];
    if (!current || !list) {
        release(current);
        release(list);
        return;
    }
    if (forward) {
        if (kui_element_next_sibling(current, &target) != KUI_OK) kui_element_first_child(list, &target);
    } else {
        if (kui_element_previous_sibling(current, &target) != KUI_OK) kui_element_last_child(list, &target);
    }
    kui_element_class_list_remove(current, "cur", 0);
    if (target) kui_element_class_list_add(target, "cur", 0);
    id_of(target, id, sizeof id);
    snprintf(out, sizeof out, "#%s", id);
    kuex_set_text(g_doc, "trav-out", out);
    release(target);
    release(current);
    release(list);
}

static void on_trav_next(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    move_highlight(1);
}

static void on_trav_prev(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    move_highlight(0);
}

static void on_trav_info(KuiEvent* event, void* user_data) {
    KuiElement* current = qs("#trav-list li.cur");
    KuiElement* parent = NULL;
    KuiElement* panel = NULL;
    int mid = 0;
    char a[64], b[64], out[160];
    (void)event;
    (void)user_data;
    if (current) {
        kui_element_parent(current, &parent);
        kui_element_closest(current, "section.panel", 0, &panel);
        kui_element_matches(current, ".mid", 0, &mid);
    }
    id_of(parent, a, sizeof a);
    id_of(panel, b, sizeof b);
    snprintf(out, sizeof out, "parent=#%s | closest(.panel)=#%s | matches(.mid)=%s", a, b,
             mid ? "yes" : "no");
    kuex_set_text(g_doc, "trav-out", out);
    release(parent);
    release(panel);
    release(current);
}

static void show_build_count(KuiElement* box) {
    char out[48];
    snprintf(out, sizeof out, "children=%zu", children_of(box));
    kuex_set_text(g_doc, "build-out", out);
}

static void on_build_append(KuiEvent* event, void* user_data) {
    KuiElement* box = kuex_by_id(g_doc, "build-box");
    KuiElement* item = NULL;
    char text[32];
    (void)event;
    (void)user_data;
    snprintf(text, sizeof text, "item %d", ++g_build_serial);
    if (box && kui_document_create_element(g_doc, "p", 0, &item) == KUI_OK) {
        kui_element_set_class_name(item, "item", 0);
        kui_element_set_text_content(item, text, 0);
        kui_element_append_child(box, item);
        release(item);
    }
    show_build_count(box);
    release(box);
}

static void on_build_prepend(KuiEvent* event, void* user_data) {
    KuiElement* box = kuex_by_id(g_doc, "build-box");
    KuiElement* first = NULL;
    KuiElement* item = NULL;
    char text[32];
    (void)event;
    (void)user_data;
    snprintf(text, sizeof text, "first %d", ++g_build_serial);
    if (box && kui_document_create_element(g_doc, "p", 0, &item) == KUI_OK) {
        kui_element_set_class_name(item, "item", 0);
        kui_element_set_text_content(item, text, 0);
        kui_element_first_child(box, &first);
        kui_element_insert_before(box, item, first); /* NULL first appends */
        release(item);
        release(first);
    }
    show_build_count(box);
    release(box);
}

static void on_build_clone(KuiEvent* event, void* user_data) {
    KuiElement* box = kuex_by_id(g_doc, "build-box");
    KuiElement* seed = kuex_by_id(g_doc, "build-seed");
    KuiElement* copy = NULL;
    char id[32];
    (void)event;
    (void)user_data;
    if (box && seed && kui_element_clone(seed, 1, &copy) == KUI_OK) {
        snprintf(id, sizeof id, "seed-copy-%d", ++g_build_serial);
        kui_element_set_attribute(copy, "id", 0, id, 0);
        kui_element_append_child(box, copy);
        release(copy);
    }
    show_build_count(box);
    release(seed);
    release(box);
}

static void on_build_replace(KuiEvent* event, void* user_data) {
    KuiElement* box = kuex_by_id(g_doc, "build-box");
    KuiElement* first = NULL;
    KuiElement* item = NULL;
    char text[32];
    (void)event;
    (void)user_data;
    if (box) kui_element_first_child(box, &first);
    if (first && kui_document_create_element(g_doc, "p", 0, &item) == KUI_OK) {
        snprintf(text, sizeof text, "replaced %d", ++g_build_serial);
        kui_element_set_class_name(item, "replaced", 0);
        kui_element_set_text_content(item, text, 0);
        kui_element_replace_child(box, item, first);
        release(item);
    }
    show_build_count(box);
    release(first);
    release(box);
}

static void show_rm_state(void) {
    KuiElement* box = kuex_by_id(g_doc, "rm-box");
    int flagged = 0;
    char out[64];
    if (box) kui_element_has_attribute(box, "data-flag", 0, &flagged);
    snprintf(out, sizeof out, "left=%zu | data-flag=%s", children_of(box), flagged ? "on" : "removed");
    kuex_set_text(g_doc, "rm-out", out);
    release(box);
}

static void on_rm_last(KuiEvent* event, void* user_data) {
    KuiElement* box = kuex_by_id(g_doc, "rm-box");
    KuiElement* last = NULL;
    (void)event;
    (void)user_data;
    if (box && kui_element_last_child(box, &last) == KUI_OK) kui_element_remove(last);
    release(last);
    release(box);
    show_rm_state();
}

static void on_rm_first(KuiEvent* event, void* user_data) {
    KuiElement* box = kuex_by_id(g_doc, "rm-box");
    KuiElement* first = NULL;
    (void)event;
    (void)user_data;
    if (box && kui_element_first_child(box, &first) == KUI_OK) kui_element_remove_child(box, first);
    release(first);
    release(box);
    show_rm_state();
}

static void on_rm_attr(KuiEvent* event, void* user_data) {
    KuiElement* box = kuex_by_id(g_doc, "rm-box");
    int flagged = 0;
    (void)event;
    (void)user_data;
    if (box) {
        kui_element_has_attribute(box, "data-flag", 0, &flagged);
        if (flagged) kui_element_remove_attribute(box, "data-flag", 0);
        else kui_element_set_attribute(box, "data-flag", 0, "on", 0);
    }
    release(box);
    show_rm_state();
}

static void on_rm_retain(KuiEvent* event, void* user_data) {
    KuiElement* box = kuex_by_id(g_doc, "rm-box");
    char id[64], out[96];
    (void)event;
    (void)user_data;
    if (box && kui_element_retain(box) == KUI_OK) kui_element_release(box);
    id_of(box, id, sizeof id);
    snprintf(out, sizeof out, "retained handle -> #%s", id);
    kuex_set_text(g_doc, "rm-out", out);
    release(box);
}

static void on_html_reset(KuiEvent* event, void* user_data) {
    KuiElement* box = kuex_by_id(g_doc, "html-box");
    char html[256];
    (void)event;
    (void)user_data;
    if (box) kui_element_set_inner_html(box, "<span id=\"html-core\">core</span>", 0);
    inner_html_of(box, html, sizeof html);
    kuex_set_text(g_doc, "html-out", html);
    release(box);
}

static void on_html_adjacent(KuiEvent* event, void* user_data) {
    KuiElement* core = kuex_by_id(g_doc, "html-core");
    char text[128];
    (void)event;
    (void)user_data;
    if (core) {
        kui_element_insert_adjacent_html(core, "beforebegin", 0, "<i>[</i>", 0);
        kui_element_insert_adjacent_html(core, "afterbegin", 0, "<i>(</i>", 0);
        kui_element_insert_adjacent_html(core, "beforeend", 0, "<i>)</i>", 0);
        kui_element_insert_adjacent_html(core, "afterend", 0, "<i>]</i>", 0);
    }
    kuex_get_text(g_doc, "html-box", text, sizeof text);
    kuex_set_text(g_doc, "html-out", text);
    release(core);
}

static void on_html_text(KuiEvent* event, void* user_data) {
    KuiElement* core = kuex_by_id(g_doc, "html-core");
    char text[128];
    (void)event;
    (void)user_data;
    if (core) kui_element_append_text(core, "+", 0);
    kuex_element_text(core, text, sizeof text);
    kuex_set_text(g_doc, "html-out", text);
    release(core);
}

static void on_html_outer(KuiEvent* event, void* user_data) {
    KuiElement* core = kuex_by_id(g_doc, "html-core");
    char html[256];
    size_t length = 0;
    (void)event;
    (void)user_data;
    html[0] = '\0';
    if (core) kui_element_get_outer_html(core, html, sizeof html, &length);
    kuex_set_text(g_doc, "html-out", html);
    release(core);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    int failed = 0;
    g_doc = host->document;

    check_lookups();
    check_traversal();
    check_create_insert();
    check_remove_attributes();
    check_html_text();
    check_form_state();
    finish();

    failed |= kuex_on(g_doc, "btn-lookup-count", "click", on_lookup_count, NULL);
    failed |= kuex_on(g_doc, "btn-lookup-hit", "click", on_lookup_hit, NULL);
    failed |= kuex_on(g_doc, "btn-lookup-root", "click", on_lookup_root, NULL);
    failed |= kuex_on(g_doc, "btn-trav-prev", "click", on_trav_prev, NULL);
    failed |= kuex_on(g_doc, "btn-trav-next", "click", on_trav_next, NULL);
    failed |= kuex_on(g_doc, "btn-trav-info", "click", on_trav_info, NULL);
    failed |= kuex_on(g_doc, "btn-build-append", "click", on_build_append, NULL);
    failed |= kuex_on(g_doc, "btn-build-prepend", "click", on_build_prepend, NULL);
    failed |= kuex_on(g_doc, "btn-build-clone", "click", on_build_clone, NULL);
    failed |= kuex_on(g_doc, "btn-build-replace", "click", on_build_replace, NULL);
    failed |= kuex_on(g_doc, "btn-rm-last", "click", on_rm_last, NULL);
    failed |= kuex_on(g_doc, "btn-rm-first", "click", on_rm_first, NULL);
    failed |= kuex_on(g_doc, "btn-rm-attr", "click", on_rm_attr, NULL);
    failed |= kuex_on(g_doc, "btn-rm-retain", "click", on_rm_retain, NULL);
    failed |= kuex_on(g_doc, "btn-html-reset", "click", on_html_reset, NULL);
    failed |= kuex_on(g_doc, "btn-html-adjacent", "click", on_html_adjacent, NULL);
    failed |= kuex_on(g_doc, "btn-html-text", "click", on_html_text, NULL);
    failed |= kuex_on(g_doc, "btn-html-outer", "click", on_html_outer, NULL);
    return failed;
}
