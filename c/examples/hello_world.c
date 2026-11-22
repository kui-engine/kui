/* =========================================================================
 * hello_world.c - the KUI C ABI in plain C
 *
 * This is the ROADMAP M2 exit criterion: it must compile with MSVC, gcc and
 * clang and behave identically. No window is opened, so it runs headless in CI.
 *
 * Note what is NOT here: no CRT flavour requirement, no C++ standard setting,
 * no compiler match with the DLL. That is the point of the C ABI -- compile this
 * file however you like.
 * ========================================================================= */
#include <kui_c.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Every call returns a status; check it. */
#define CHECK(expr)                                                        \
    do {                                                                   \
        KuiStatus _s = (expr);                                             \
        if (_s != KUI_OK) {                                                \
            fprintf(stderr, "FAILED: %s -> %s (%d)\n", #expr,              \
                    kui_status_message(_s), (int)_s);                      \
            return 1;                                                      \
        }                                                                  \
    } while (0)

/* Read a string-valued property with the two-call pattern: ask for the length,
 * allocate with YOUR allocator, then fill. Nothing crosses the CRT heap. */
static char* read_text(KuiElement* el) {
    size_t need = 0;
    char*  buf;

    if (kui_element_get_text_content(el, NULL, 0, &need) != KUI_OK) return NULL;

    buf = (char*)malloc(need + 1);
    if (!buf) return NULL;

    if (kui_element_get_text_content(el, buf, need + 1, &need) != KUI_OK) {
        free(buf);
        return NULL;
    }
    return buf;
}

/* Listeners are plain C functions. State reaches them through user_data --
 * there is no capture in C, and the ABI deliberately does not invent one. */
static void on_click(KuiEvent* evt, void* user_data) {
    int*        clicks = (int*)user_data;
    KuiElement* target = NULL;
    char        type[32];
    char        tag[32];
    size_t      len = 0;

    ++(*clicks);

    kui_event_get_type(evt, type, sizeof type, &len);

    /* The KuiEvent* is valid only inside this callback and must not be stored.
     * A handle obtained FROM it, however, is an ordinary counted handle that the
     * callback has to release. */
    if (kui_event_get_target(evt, &target) == KUI_OK) {
        kui_element_get_tag_name(target, tag, sizeof tag, &len);
        printf("  listener: \"%s\" on <%s>, call #%d\n", type, tag, *clicks);
        kui_element_release(target);
    }
}

int main(void) {
    KuiVersion   ver = kui_get_version();
    KuiDocument* doc = NULL;
    KuiElement*  body = NULL;
    KuiElement*  title = NULL;
    char         tag[64];
    size_t       tag_len = 0;
    char*        text;

    printf("KUI %d.%d.%d via the C ABI\n", ver.major, ver.minor, ver.patch);

    CHECK(kui_document_create(&doc));

    CHECK(kui_document_load_html(doc,
        "<html><body>"
        "  <h1 id='title' class='greeting'>Hello from C</h1>"
        "  <p>No C++ compiler was harmed in the making of this program.</p>"
        "</body></html>", 0));   /* len 0 -> strlen */

    CHECK(kui_document_body(doc, &body));
    CHECK(kui_element_get_tag_name(body, tag, sizeof tag, &tag_len));
    printf("body tag       : <%s> (%zu bytes)\n", tag, tag_len);

    CHECK(kui_document_get_element_by_id(doc, "title", 0, &title));

    CHECK(kui_element_get_tag_name(title, tag, sizeof tag, &tag_len));
    printf("#title tag     : <%s>\n", tag);

    text = read_text(title);
    if (!text) { fprintf(stderr, "FAILED: read_text\n"); return 1; }
    printf("#title text    : \"%s\"\n", text);
    free(text);

    /* Attributes: read one that exists, and confirm absence is reported as
     * NOT_FOUND rather than an empty string. */
    {
        char   cls[64];
        size_t cls_len = 0;
        int    has = 0;

        CHECK(kui_element_get_attribute(title, "class", 0, cls, sizeof cls, &cls_len));
        printf("#title class   : \"%s\"\n", cls);

        CHECK(kui_element_has_attribute(title, "data-nope", 0, &has));
        printf("has data-nope  : %s\n", has ? "yes" : "no");

        if (kui_element_get_attribute(title, "data-nope", 0, cls, sizeof cls, &cls_len)
                != KUI_ERR_NOT_FOUND) {
            fprintf(stderr, "FAILED: missing attribute should report NOT_FOUND\n");
            return 1;
        }
    }

    /* Mutate, then read back. */
    CHECK(kui_element_set_attribute(title, "data-lang", 0, "C", 0));
    CHECK(kui_element_set_text_content(title, "Goodbye from C", 0));
    text = read_text(title);
    if (!text) { fprintf(stderr, "FAILED: read_text after set\n"); return 1; }
    printf("#title text now: \"%s\"\n", text);
    free(text);

    /* Events: register, dispatch, observe. */
    {
        int           clicks = 0;
        KuiListenerId id = 0;
        int           not_canceled = 0;

        printf("\nevents:\n");
        CHECK(kui_element_add_event_listener(title, "click", 0, on_click, &clicks, &id));
        CHECK(kui_element_dispatch_event(title, "click", 0, 1, 1, &not_canceled));
        CHECK(kui_element_dispatch_event(title, "click", 0, 1, 1, &not_canceled));
        printf("  dispatched twice, listener saw %d, not_canceled=%d\n",
               clicks, not_canceled);

        /* Removal needs the event name as well as the id. */
        CHECK(kui_element_remove_event_listener(title, "click", 0, id));
        CHECK(kui_element_dispatch_event(title, "click", 0, 1, 1, NULL));
        printf("  after removal, listener count still %d\n", clicks);
    }

    /* Release every handle we were given, in any order. */
    CHECK(kui_element_release(title));
    CHECK(kui_element_release(body));
    CHECK(kui_document_release(doc));

    printf("\nPASS\n");
    return 0;
}
