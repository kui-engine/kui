/* Host logic of examples/repro_kui011/main.cpp (KUI-011 crash repro): a 16 ms interval rebuilds
 * the main window's #list with setInnerHTML (24 rows every 8th tick, otherwise 6) while a
 * frameless 380x200 "modal" window is created, pumped by a nested message loop, destroyed and
 * re-created, round after round. Prints "NO CRASH" and exits 0 after KUI011_ROUNDS rounds
 * (default 400); KUI011_TIMEOUT_SEC (default 120) exits 3 instead.
 *
 * Like the original, everything runs before the regular event loop, pumping messages itself:
 * kui_platform_poll_events stands for pumpFor's PeekMessage loop, and each nested-loop step is
 * one kui_platform_wait_events call (GetMessageW returns one message, so KUI011_NESTED_MSGS
 * counts batches here, not single messages). Not ported: the Win32 crash handler / minidump
 * (a crash simply kills the process), KUI011_SIMPLE (would mean reloading the generated main
 * page) and InvalidateRect (setInnerHTML already schedules the repaint). The main window is
 * disabled during the modal with kui_window_set_enabled (EnableWindow, Windows only) and the
 * dialog is created hidden (kui_window_create_ex), as the original does. The runner's placeholder
 * second window is hidden: the original creates its dialog afresh every round. */
#include "kui_c_example_dom.h"

#include <stdlib.h>

static const char k_dialog_html[] =
    "<html><head><style>\n"
    "  body { margin:0; background:#11111b; color:#cdd6f4;\n"
    "         font-family:'Segoe UI',sans-serif; }\n"
    "  .box { padding:18px; transform:translateZ(0); opacity:0.99;\n"
    "         box-shadow:0 12px 40px rgba(0,0,0,0.6); }\n"
    "  .btn { margin-top:14px; padding:8px 18px; background:#89b4fa; color:#11111b;\n"
    "         border-radius:6px; display:inline-block; transform:scale(1); }\n"
    "</style></head><body>\n"
    "  <div class=\"box\">\n"
    "    <div>确认要执行该操作吗？（自绘模态消息框）</div>\n"
    "    <div class=\"btn\" id=\"ok\">确定</div>\n"
    "  </div>\n"
    "</body></html>\n";

static KuiDocument* g_doc;
static int g_tick;

static int env_int(const char* name, int fallback) {
    const char* text = getenv(name);
    int value = text ? atoi(text) : 0;
    return value > 0 ? value : fallback;
}

static double now_ms(void) {
    double ms = 0;
    kui_platform_now(&ms);
    return ms;
}

static void on_tick(void* user_data) {
    char html[24 * 160];
    size_t used = 0;
    int t = ++g_tick;
    int n = (t % 8 == 0) ? 24 : 6;
    int i;
    KuiElement* list = kuex_by_id(g_doc, "list");
    (void)user_data;
    if (!list) return;
    html[0] = '\0';
    for (i = 0; i < n; ++i) {
        used += (size_t)snprintf(html + used, sizeof html - used,
                                 "<div class=\"row layerA\"><span class=\"pill\">#%d.%d</span>"
                                 " 扫描扇区 %d</div>",
                                 t, i, (t * 31 + i) % 100000);
    }
    kui_element_set_inner_html(list, html, 0);
    kui_element_release(list);
}

static void pump_for(double ms) {
    double start = now_ms();
    while (now_ms() - start < ms) kui_platform_wait_events(0.001);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    const int rounds = env_int("KUI011_ROUNDS", 400);
    const int nested_msgs = env_int("KUI011_NESTED_MSGS", 80);
    const int watchdog_sec = env_int("KUI011_TIMEOUT_SEC", 120);
    const double started = now_ms();
    KuiTimerId timer = 0;
    int round;

    g_doc = host->document;
    if (host->second_window) kui_window_hide(host->second_window);
    if (kui_platform_set_interval(on_tick, NULL, 16, &timer) != KUI_OK) return 1;

    pump_for(300); /* let the main window settle */
    fprintf(stderr, "[repro] start: rounds=%d nestedMsgs=%d timeout=%ds\n", rounds, nested_msgs,
            watchdog_sec);

    for (round = 0; round < rounds; ++round) {
        KuiWindowProps props;
        KuiWindowPropsEx ex;
        KuiWindow* dialog = NULL;
        KuiDocument* dialog_doc = NULL;
        int step;

        if (now_ms() - started > watchdog_sec * 1000.0) {
            fprintf(stderr, "[repro] WATCHDOG TIMEOUT (%ds) — exiting (no crash)\n", watchdog_sec);
            kui_platform_quit(3);
            return 0;
        }
        memset(&props, 0, sizeof props);
        props.title = "modal";
        props.width = 380;
        props.height = 200;
        props.centered = 1;
        props.frameless = 1;
        props.no_decorations = 1;
        memset(&ex, 0, sizeof ex);
        ex.struct_size = sizeof ex;
        ex.base = props;
        ex.hidden = 1; /* the original creates it with visible = false */
        if (kui_window_create_ex(&ex, &dialog) != KUI_OK) {
            fprintf(stderr, "[repro] dlg create failed @round %d\n", round);
            break;
        }
        if (kui_window_get_document(dialog, &dialog_doc) == KUI_OK) {
            kui_document_load_html(dialog_doc, k_dialog_html, 0);
            kui_document_release(dialog_doc);
        }
        kui_window_set_enabled(host->window, 0); /* standard modal: disable the parent (Windows only) */
        kui_window_show(dialog);

        /* Nested loop: dispatches every window's messages (main timer, paints) while the
         * dialog is up; after nested_msgs steps the "user" closes it. */
        for (step = 0; step < nested_msgs; ++step) kui_platform_wait_events(0.1);

        kui_window_set_enabled(host->window, 1);
        kui_window_release(dialog); /* dlg.reset(): destroys the window and its document */
        pump_for(20);               /* back to the main loop for a few frames */

        if (round % 25 == 0) fprintf(stderr, "[repro] round %d / %d (tick=%d)\n", round, rounds,
                                     g_tick);
    }

    fprintf(stderr, "[repro] NO CRASH after %d rounds (tick=%d)\n", round, g_tick);
    kui_platform_clear_timer(timer);
    kui_platform_quit(0);
    return 0;
}
