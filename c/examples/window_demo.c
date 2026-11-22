/* =========================================================================
 * window_demo.c - a real GUI application in plain C
 *
 * hello_world.c stays headless so it can run in CI. This one actually opens a
 * window and enters the event loop, which is the part of the API that cannot be
 * asserted from a unit test.
 *
 * It quits itself after a few seconds via a timer, so it can be run unattended
 * (and it also demonstrates timers, which is the other thing a unit test cannot
 * exercise -- callbacks only fire while the loop is running).
 *
 * Pass a number of seconds as argv[1] to change the lifetime; 0 means "stay open
 * until closed by hand".
 * ========================================================================= */
#include <kui_c.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                        \
    do {                                                                   \
        KuiStatus _s = (expr);                                             \
        if (_s != KUI_OK) {                                                \
            fprintf(stderr, "FAILED: %s -> %s\n", #expr,                   \
                    kui_status_message(_s));                               \
            return 1;                                                      \
        }                                                                  \
    } while (0)

static const char* kHtml =
"<!DOCTYPE html><html><head><style>"
"  * { margin: 0; padding: 0; box-sizing: border-box; }"
"  body { display: flex; flex-direction: column; align-items: center;"
"         justify-content: center; height: 100vh;"
"         background: linear-gradient(135deg, #1e3a5f 0%, #2d1b4e 100%);"
"         font-family: 'Segoe UI', sans-serif; color: #e8e8f0; }"
"  h1 { font-size: 34px; letter-spacing: 1px; }"
"  p  { margin-top: 14px; font-size: 15px; color: #9aa4c0; }"
"  #ticks { margin-top: 22px; font-size: 13px; color: #6d7a99; }"
"</style></head><body>"
"  <h1 id='title'>Hello from C</h1>"
"  <p>This window was created through the KUI C ABI.</p>"
"  <div id='ticks'>tick 0</div>"
"</body></html>";

/* Interval callback: updates the DOM from a timer, which is the normal way a C
 * program drives the UI. */
static void on_tick(void* user_data) {
    KuiDocument* doc = (KuiDocument*)user_data;
    KuiElement*  ticks = NULL;
    static int   n = 0;
    char         text[64];

    ++n;
    if (kui_document_get_element_by_id(doc, "ticks", 0, &ticks) == KUI_OK) {
        snprintf(text, sizeof text, "tick %d", n);
        kui_element_set_text_content(ticks, text, 0);
        kui_element_release(ticks);
    }
}

static void on_quit_timer(void* user_data) {
    (void)user_data;
    printf("lifetime elapsed, quitting\n");
    kui_platform_quit(0);
}

int main(int argc, char** argv) {
    KuiWindowProps props;
    KuiWindow*     win = NULL;
    KuiDocument*   doc = NULL;
    KuiVersion     ver = kui_get_version();
    int            exit_code = 0;
    int            seconds = 4;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc > 1) seconds = atoi(argv[1]);

    printf("KUI %d.%d.%d - opening a window from C\n", ver.major, ver.minor, ver.patch);

    CHECK(kui_platform_init());

    /* Zero first: every field treats 0 as "library default", so only what
     * matters needs setting. */
    memset(&props, 0, sizeof props);
    props.title    = "KUI C ABI - window demo";
    props.width    = 720;
    props.height   = 460;
    props.centered = 1;

    CHECK(kui_window_create(&props, &win));

    /* The window owns its document; this hands back a counted reference. */
    CHECK(kui_window_get_document(win, &doc));
    CHECK(kui_document_load_html(doc, kHtml, 0));

    /* Created windows are already visible, but showing again is harmless and
     * makes the intent explicit. */
    CHECK(kui_window_show(win));

    /* Drive the UI from a repeating timer. */
    CHECK(kui_platform_set_interval(on_tick, doc, 1000, NULL));

    if (seconds > 0) {
        printf("will quit on its own after %d seconds\n", seconds);
        CHECK(kui_platform_set_timeout(on_quit_timer, NULL, (uint32_t)(seconds * 1000), NULL));
    } else {
        printf("close the window to quit\n");
    }

    /* Blocks until quit. */
    CHECK(kui_platform_run(&exit_code));
    printf("event loop returned %d\n", exit_code);

    kui_document_release(doc);
    kui_window_release(win);   /* destroys the window */
    CHECK(kui_platform_shutdown());

    printf("PASS\n");
    return 0;
}
