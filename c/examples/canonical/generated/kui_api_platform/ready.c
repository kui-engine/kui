/* Copied by scripts/generate_c_examples.py from examples/c/ports/kui_api_platform.c; edit that file, not this copy. */
/* API tour: platform services (examples/api_tours/platform). The host reads the monotonic clock,
 * runs timeouts / intervals / idle callbacks / posted tasks, pumps the event loop by hand
 * (poll_events, wait_events), round-trips the clipboard (putting the original text back),
 * queries the monitors and toggles F12 DevTools. One #tour-log row per check; #tour-result ends
 * as "PASS n/n" once the timers have fired. Message box, file dialogs and open_url are only
 * behind (grey) buttons: they are modal or open the system browser. */
#include "kui_c_example_dom.h"

#include <stdarg.h>
#include <stdlib.h>

static KuiDocument* g_doc;
static KuiWindow* g_window;

/* ---- Self-check bookkeeping: count, log one <tr> per check, publish the verdict ----------
 * check():  a documented behaviour; a failure makes the tour FAIL.
 * skip():   not possible in this environment; logged, not counted. */

static int g_total;
static int g_failed;
static char g_failed_names[512];

static void log_row(const char* name, const char* result, const char* cell) {
    KuiElement* log = kuex_by_id(g_doc, "tour-log");
    KuiElement* row = NULL;
    if (!log) return;
    if (kui_document_create_element(g_doc, "tr", 0, &row) == KUI_OK) {
        kui_element_set_attribute(row, "data-check", 0, name, 0);
        kui_element_set_attribute(row, "data-result", 0, result, 0);
        if (strcmp(result, "fail") == 0) kui_element_class_list_add(row, "bad", 0);
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

static void skip(const char* name, const char* why) {
    char cell[256];
    snprintf(cell, sizeof cell, "skipped: %s", why);
    log_row(name, "skip", cell);
}

static void finish(void) {
    char text[640];
    char number[16];
    KuiElement* result = kuex_by_id(g_doc, "tour-result");
    if (!result) return;
    if (g_failed == 0) {
        snprintf(text, sizeof text, "PASS %d/%d", g_total, g_total);
    } else {
        snprintf(text, sizeof text, "FAIL %d/%d: %s", g_failed, g_total, g_failed_names);
    }
    kui_element_set_text_content(result, text, 0);
    snprintf(number, sizeof number, "%d", g_total);
    kui_element_set_attribute(result, "data-total", 0, number, 0);
    snprintf(number, sizeof number, "%d", g_failed);
    kui_element_set_attribute(result, "data-failed", 0, number, 0);
    kui_element_set_attribute(result, "data-known", 0, "0", 0);
    kui_element_class_list_add(result, g_failed == 0 ? "pass" : "fail", 0);
    kui_element_release(result);
}

static double now_ms(void) {
    double value = 0.0;
    kui_platform_now(&value);
    return value;
}

/* ---- Clock, timers, idle callbacks, posted tasks ------------------------------------------ */

static double g_timeout_started;
static double g_timeout_elapsed = -1.0;
static int g_interval_ticks;
static KuiTimerId g_interval_id;
static int g_cancelled_fired;
static int g_idle_ran;
static int g_posted_ran;

static void on_timeout(void* user_data) {
    (void)user_data;
    g_timeout_elapsed = now_ms() - g_timeout_started;
}

/* Clears itself on the third tick. */
static void on_interval(void* user_data) {
    (void)user_data;
    if (++g_interval_ticks == 3) kui_platform_clear_timer(g_interval_id);
}

static void on_cancelled(void* user_data) {
    (void)user_data;
    g_cancelled_fired++;
}

static void on_idle(void* user_data) {
    (void)user_data;
    g_idle_ran++;
}

static void on_posted(void* user_data) {
    (void)user_data;
    g_posted_ran++;
}

static void check_clock_and_timers(void) {
    KuiTimerId timer = 0, cancelled = 0;
    double first, second;
    KuiStatus status;

    status = kui_platform_now(&first);
    second = now_ms();
    check("time.now", status == KUI_OK && first > 0.0 && second >= first, "status %d now %.3f then %.3f",
          status, first, second);

    g_timeout_started = now_ms();
    status = kui_platform_set_timeout(on_timeout, NULL, 50, &timer);
    check("time.set_timeout", status == KUI_OK && timer != 0, "status %d id %llu", status,
          (unsigned long long)timer);
    status = kui_platform_set_interval(on_interval, NULL, 30, &g_interval_id);
    check("time.set_interval", status == KUI_OK && g_interval_id != 0 && g_interval_id != timer,
          "status %d id %llu", status, (unsigned long long)g_interval_id);
    kui_platform_set_timeout(on_cancelled, NULL, 40, &cancelled);
    status = kui_platform_clear_timer(cancelled);
    check("time.clear_timer", status == KUI_OK, "status %d", status);
    status = kui_platform_request_idle(on_idle, NULL);
    check("time.request_idle", status == KUI_OK, "status %d", status);
}

/* ---- Pumping the loop by hand: a posted task runs inside poll_events / wait_events -------- */

static void check_event_loop(void) {
    KuiStatus status;
    double started, elapsed;
    int before;

    status = kui_platform_post_to_main_thread(on_posted, NULL, NULL);
    check("loop.post_to_main_thread-async", status == KUI_OK && g_posted_ran == 0,
          "status %d, ran %d time(s) before the loop", status, g_posted_ran);
    status = kui_platform_poll_events();
    check("loop.poll_events-runs-posted-task", status == KUI_OK && g_posted_ran == 1,
          "status %d, posted task ran %d time(s)", status, g_posted_ran);

    started = now_ms();
    status = kui_platform_wait_events(0.05);
    elapsed = now_ms() - started;
    check("loop.wait_events-timeout", status == KUI_OK && elapsed < 1000.0,
          "status %d returned after %.1f ms", status, elapsed);
    /* A posted task wakes a waiting loop at once. */
    before = g_posted_ran;
    kui_platform_post_to_main_thread(on_posted, NULL, NULL);
    started = now_ms();
    status = kui_platform_wait_events(2.0);
    elapsed = now_ms() - started;
    if (g_posted_ran == before) kui_platform_poll_events();
    check("loop.wait_events-woken-by-post", status == KUI_OK && g_posted_ran == before + 1 &&
                                                 elapsed < 1500.0,
          "status %d, ran %d -> %d, waited %.1f ms of 2000", status, before, g_posted_ran, elapsed);
}

/* ---- Clipboard round trip (original text restored) ----------------------------------------- */

static const char* k_sample = "KUI clipboard \xe4\xbd\xa0\xe5\xa5\xbd 42";

/* Clipboard text in a malloc'ed buffer (caller frees); NULL on failure. */
static char* clipboard_copy(void) {
    size_t length = 0;
    char* text;
    if (kui_platform_get_clipboard_text(NULL, 0, &length) != KUI_OK) return NULL;
    text = (char*)malloc(length + 1);
    if (!text) return NULL;
    if (kui_platform_get_clipboard_text(text, length + 1, &length) != KUI_OK) {
        free(text);
        return NULL;
    }
    return text;
}

/* Saves, writes the sample, reads it back, restores. Writes a summary into out. */
static int clipboard_round_trip(char* out, size_t capacity, int self_check) {
    char small[4];
    size_t length = 0;
    int has = 0, has_after = 0, ok;
    char* original;
    char* read_back;
    char* restored;
    KuiStatus status;

    kui_platform_has_clipboard_text(&has);
    if (!has) {
        snprintf(out, capacity, "clipboard holds no text: left alone");
        return -1;
    }
    original = clipboard_copy();
    if (!original) {
        snprintf(out, capacity, "could not read the clipboard");
        return 0;
    }
    status = kui_platform_set_clipboard_text(k_sample, 0);
    kui_platform_has_clipboard_text(&has_after);
    read_back = clipboard_copy();
    ok = status == KUI_OK && has_after == 1 && read_back && strcmp(read_back, k_sample) == 0;
    if (self_check) {
        check("clip.set_clipboard_text", status == KUI_OK && has_after == 1, "status %d has %d",
              status, has_after);
        check("clip.get_clipboard_text", read_back && strcmp(read_back, k_sample) == 0,
              "read back '%s'", read_back ? read_back : "(null)");
        status = kui_platform_get_clipboard_text(small, sizeof small, &length);
        check("clip.get_clipboard_text-buffer-too-small",
              status == KUI_ERR_BUFFER_TOO_SMALL && length == strlen(k_sample),
              "status %d length %u", status, (unsigned)length);
    }
    kui_platform_set_clipboard_text(original, 0);
    restored = clipboard_copy();
    if (self_check) {
        check("clip.restored", restored && strcmp(restored, original) == 0,
              "original of %u bytes, now %u bytes", (unsigned)strlen(original),
              restored ? (unsigned)strlen(restored) : 0u);
    }
    snprintf(out, capacity, "round trip %s (sample of %u bytes), original text (%u bytes) restored=%d",
             ok ? "ok" : "FAILED", (unsigned)strlen(k_sample), (unsigned)strlen(original),
             restored && strcmp(restored, original) == 0);
    free(original);
    free(read_back);
    free(restored);
    return ok;
}

static void check_clipboard(void) {
    char summary[200];
    int has = -1;
    KuiStatus status = kui_platform_has_clipboard_text(&has);
    check("clip.has_clipboard_text", status == KUI_OK && (has == 0 || has == 1), "status %d has %d",
          status, has);
    if (clipboard_round_trip(summary, sizeof summary, 1) < 0) {
        skip("clip.round-trip", "the clipboard holds no text now; it is not overwritten");
    }
}

/* ---- Monitors ----------------------------------------------------------------------------- */

static int same_rect(const KuiCMonitorInfo* a, const KuiCMonitorInfo* b) {
    return a->bounds_x == b->bounds_x && a->bounds_y == b->bounds_y &&
           a->bounds_width == b->bounds_width && a->bounds_height == b->bounds_height;
}

static int contains(const KuiCMonitorInfo* m, float x, float y) {
    return x >= m->bounds_x && y >= m->bounds_y && x < m->bounds_x + m->bounds_width &&
           y < m->bounds_y + m->bounds_height;
}

/* ---- Diagnostics: console, stack trace writer, crash reporter, input method ------------------- */

static void check_diagnostics(void) {
    KuiCStackTraceConfig stack;
    KuiCCrashReporterConfig crash;
    int stack_installed = -1;
    int crash_installed = -1;
    KuiStatus status;
    KuiStatus uninstall_status;

    status = kui_console_set_enabled(1);
    if (status == KUI_OK) status = kui_console_set_stderr_output(0);
    if (status == KUI_OK) status = kui_console_set_min_level(KUI_C_LOG_DEBUG);
    check("diag.console_configure", status == KUI_OK, "status %d", (int)status);

    memset(&stack, 0, sizeof stack);
    stack.struct_size = sizeof stack;
    stack.output_mode = KUI_C_STACK_TRACE_STDERR;
    status = kui_stack_trace_install(&stack, &stack_installed);
    uninstall_status = kui_stack_trace_uninstall();
    check("diag.stack_trace_install_uninstall", status == KUI_OK && uninstall_status == KUI_OK,
          "install %d (installed %d), uninstall %d", (int)status, stack_installed, (int)uninstall_status);

    /* Without crashpad_handler next to the executable the call succeeds and installed is 0. */
    memset(&crash, 0, sizeof crash);
    crash.struct_size = sizeof crash;
    status = kui_crash_reporter_install(&crash, &crash_installed);
    uninstall_status = kui_crash_reporter_uninstall();
    check("diag.crash_reporter_install_uninstall", status == KUI_OK && uninstall_status == KUI_OK,
          "install %d (installed %d), uninstall %d", (int)status, crash_installed, (int)uninstall_status);

    /* Windows turns the input method off for this thread; other platforms report NOT_SUPPORTED. */
    status = kui_platform_disable_ime();
    check("diag.disable_ime", status == KUI_OK || status == KUI_ERR_NOT_SUPPORTED, "status %d", (int)status);
}

static void check_monitors(void) {
    KuiCMonitorInfo primary, monitor, from_point, from_window, listed_primary;
    char name[128];
    size_t count = 0, length = 0, index;
    int primaries = 0, valid = 1, wx = 0, wy = 0, ww = 0, wh = 0;
    float cx, cy;
    void* handle = NULL;
    KuiStatus status;

    memset(&listed_primary, 0, sizeof listed_primary);
    status = kui_platform_get_monitor_count(&count);
    check("mon.get_monitor_count", status == KUI_OK && count >= 1, "status %d count %u", status,
          (unsigned)count);
    for (index = 0; index < count; ++index) {
        if (kui_platform_get_monitor(index, &monitor, name, sizeof name, &length) != KUI_OK ||
            monitor.bounds_width <= 0.0f || monitor.bounds_height <= 0.0f ||
            monitor.work_width <= 0.0f || monitor.work_width > monitor.bounds_width ||
            monitor.scale <= 0.0f) {
            valid = 0;
        }
        if (monitor.is_primary) {
            primaries++;
            listed_primary = monitor;
        }
    }
    check("mon.get_monitor", valid && primaries == 1, "valid %d, %d primary monitor(s)", valid,
          primaries);
    status = kui_platform_get_monitor(count, &monitor, NULL, 0, &length);
    check("mon.get_monitor-out-of-range", status != KUI_OK, "index %u -> status %d",
          (unsigned)count, status);

    status = kui_platform_get_primary_monitor(&primary);
    check("mon.get_primary_monitor", status == KUI_OK && primary.is_primary == 1 &&
                                         same_rect(&primary, &listed_primary),
          "status %d bounds %.0fx%.0f at (%.0f, %.0f)", status, primary.bounds_width,
          primary.bounds_height, primary.bounds_x, primary.bounds_y);

    cx = primary.bounds_x + primary.bounds_width * 0.5f;
    cy = primary.bounds_y + primary.bounds_height * 0.5f;
    status = kui_platform_get_monitor_from_point(cx, cy, &from_point, name, sizeof name, &length);
    check("mon.get_monitor_from_point", status == KUI_OK && same_rect(&from_point, &primary) &&
                                            from_point.is_primary == 1,
          "status %d bounds %.0fx%.0f at (%.0f, %.0f)", status, from_point.bounds_width,
          from_point.bounds_height, from_point.bounds_x, from_point.bounds_y);

    kui_window_get_native_handle(g_window, &handle);
    kui_window_get_position(g_window, &wx, &wy);
    kui_window_get_size(g_window, &ww, &wh);
    status = kui_platform_get_monitor_from_window(handle, &from_window, name, sizeof name, &length);
    check("mon.get_monitor_from_window",
          status == KUI_OK && contains(&from_window, wx + ww * 0.5f, wy + wh * 0.5f),
          "status %d window centre (%.0f, %.0f), monitor %.0fx%.0f at (%.0f, %.0f)", status,
          wx + ww * 0.5f, wy + wh * 0.5f, from_window.bounds_width, from_window.bounds_height,
          from_window.bounds_x, from_window.bounds_y);
    status = kui_platform_get_monitor_from_window(NULL, &from_window, NULL, 0, &length);
    check("mon.get_monitor_from_window-null", status == KUI_ERR_INVALID_ARG, "status %d", status);
}

/* ---- F12 DevTools switch (default off) --------------------------------------------------- */

static void check_f12(void) {
    KuiStatus on = kui_platform_set_f12_devtools_enabled(1);
    KuiStatus off = kui_platform_set_f12_devtools_enabled(0);
    check("shell.set_f12_devtools_enabled", on == KUI_OK && off == KUI_OK, "on %d off %d", on, off);
}

/* ---- Verdict once the timers have had their turn ---------------------------------------- */

static void finalize(void* user_data) {
    (void)user_data;
    check("time.set_timeout-fired", g_timeout_elapsed >= 45.0, "fired after %.1f ms (-1 = never)",
          g_timeout_elapsed);
    check("time.set_interval-ticks", g_interval_ticks == 3, "%d tick(s), cleared on the third",
          g_interval_ticks);
    check("time.clear_timer-cancelled", g_cancelled_fired == 0, "cleared timeout ran %d time(s)",
          g_cancelled_fired);
    check("time.request_idle-ran", g_idle_ran == 1, "idle callback ran %d time(s)", g_idle_ran);
    finish();
}

/* ---- Panel buttons ----------------------------------------------------------------------- */

static double g_button_started;

static void on_button_timeout(void* user_data) {
    char text[96];
    (void)user_data;
    snprintf(text, sizeof text, "timeout fired after %s100 ms",
             now_ms() - g_button_started >= 95.0 ? ">= " : "< ");
    kuex_set_text(g_doc, "time-out", text);
}

static void on_timeout_btn(KuiEvent* e, void* u) {
    KuiTimerId id = 0;
    (void)e;
    (void)u;
    g_button_started = now_ms();
    kui_platform_set_timeout(on_button_timeout, NULL, 100, &id);
    kuex_set_text(g_doc, "time-out", "waiting 100 ms...");
}

static int g_button_ticks;
static KuiTimerId g_button_interval;

static void on_button_tick(void* user_data) {
    char text[64];
    (void)user_data;
    if (++g_button_ticks == 3) kui_platform_clear_timer(g_button_interval);
    snprintf(text, sizeof text, "interval tick %d of 3", g_button_ticks);
    kuex_set_text(g_doc, "time-out", text);
}

static void on_interval_btn(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    g_button_ticks = 0;
    kui_platform_set_interval(on_button_tick, NULL, 50, &g_button_interval);
}

static void on_button_never(void* user_data) {
    (void)user_data;
    kuex_set_text(g_doc, "time-out", "the cleared timer fired (bug)");
}

static void on_cancel_btn(KuiEvent* e, void* u) {
    KuiTimerId id = 0;
    char text[64];
    (void)e;
    (void)u;
    kui_platform_set_timeout(on_button_never, NULL, 30, &id);
    snprintf(text, sizeof text, "timer set and cleared: status %d", kui_platform_clear_timer(id));
    kuex_set_text(g_doc, "time-out", text);
}

static void on_button_idle(void* user_data) {
    (void)user_data;
    kuex_set_text(g_doc, "time-out", "idle callback ran");
}

static void on_idle_btn(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_platform_request_idle(on_button_idle, NULL);
}

static void on_button_posted(void* user_data) {
    (void)user_data;
    kuex_set_text(g_doc, "time-out", "posted task ran on the UI thread");
}

static void on_post_btn(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_platform_post_to_main_thread(on_button_posted, NULL, NULL);
}

static int g_poll_ran;

static void on_poll_task(void* user_data) {
    (void)user_data;
    g_poll_ran++;
}

static void on_poll_btn(KuiEvent* e, void* u) {
    char text[96];
    int before = g_poll_ran;
    (void)e;
    (void)u;
    kui_platform_post_to_main_thread(on_poll_task, NULL, NULL);
    kui_platform_poll_events();
    snprintf(text, sizeof text, "posted task ran inside poll_events: %s",
             g_poll_ran == before + 1 ? "yes" : "no");
    kuex_set_text(g_doc, "loop-out", text);
}

static void on_wait_btn(KuiEvent* e, void* u) {
    char text[96];
    double started = now_ms();
    KuiStatus status;
    (void)e;
    (void)u;
    status = kui_platform_wait_events(0.05);
    snprintf(text, sizeof text, "wait_events(0.05): status %d, returned within 1 s: %s", status,
             now_ms() - started < 1000.0 ? "yes" : "no");
    kuex_set_text(g_doc, "loop-out", text);
}

static void on_clip_read(KuiEvent* e, void* u) {
    char text[96];
    int has = 0;
    char* copy;
    (void)e;
    (void)u;
    kui_platform_has_clipboard_text(&has);
    copy = has ? clipboard_copy() : NULL;
    snprintf(text, sizeof text, "has text=%d (%u bytes)", has, copy ? (unsigned)strlen(copy) : 0u);
    free(copy);
    kuex_set_text(g_doc, "clip-out", text);
}

static void on_clip_trip(KuiEvent* e, void* u) {
    char summary[200];
    (void)e;
    (void)u;
    clipboard_round_trip(summary, sizeof summary, 0);
    kuex_set_text(g_doc, "clip-out", summary);
}

static void describe(const KuiCMonitorInfo* m, const char* name, char* out, size_t capacity) {
    snprintf(out, capacity, "%s%s%.0fx%.0f at (%.0f, %.0f) work %.0fx%.0f scale %.2f%s", name,
             name[0] ? ": " : "", m->bounds_width, m->bounds_height, m->bounds_x, m->bounds_y,
             m->work_width, m->work_height, m->scale, m->is_primary ? " primary" : "");
}

static void on_mon_list(KuiEvent* e, void* u) {
    char text[600];
    char line[200];
    char name[128];
    size_t count = 0, index, length = 0, used;
    KuiCMonitorInfo monitor;
    (void)e;
    (void)u;
    kui_platform_get_monitor_count(&count);
    snprintf(text, sizeof text, "%u monitor(s)", (unsigned)count);
    for (index = 0; index < count; ++index) {
        name[0] = '\0';
        if (kui_platform_get_monitor(index, &monitor, name, sizeof name, &length) != KUI_OK) continue;
        describe(&monitor, name, line, sizeof line);
        used = strlen(text);
        snprintf(text + used, sizeof text - used, " | %s", line);
    }
    kuex_set_text(g_doc, "mon-out", text);
}

static void on_mon_window(KuiEvent* e, void* u) {
    char text[240];
    char line[200];
    char name[128] = "";
    size_t length = 0;
    void* handle = NULL;
    KuiCMonitorInfo monitor;
    (void)e;
    (void)u;
    kui_window_get_native_handle(g_window, &handle);
    if (kui_platform_get_monitor_from_window(handle, &monitor, name, sizeof name, &length) != KUI_OK) {
        kuex_set_text(g_doc, "mon-out", "monitor lookup failed");
        return;
    }
    describe(&monitor, name, line, sizeof line);
    snprintf(text, sizeof text, "this window: %s", line);
    kuex_set_text(g_doc, "mon-out", text);
}

/* Manual: modal. */
static void on_msg(KuiEvent* e, void* u) {
    KuiCMessageBoxResult result = KUI_C_MESSAGE_BOX_RESULT_CANCEL;
    char text[64];
    (void)e;
    (void)u;
    kui_platform_message_box("KUI API tour", 0, "Do you like message boxes?", 0,
                             KUI_C_MESSAGE_BOX_QUESTION, KUI_C_MESSAGE_BOX_YES_NO, &result);
    snprintf(text, sizeof text, "message box answer: %s",
             result == KUI_C_MESSAGE_BOX_RESULT_YES ? "yes" : "no");
    kuex_set_text(g_doc, "shell-out", text);
}

static const char* const k_filters[] = {"HTML pages|*.html;*.htm", "All files|*.*"};

static void show_path(const char* what, KuiStatus status, const char* path) {
    char text[600];
    if (status == KUI_ERR_NOT_FOUND) {
        snprintf(text, sizeof text, "%s: cancelled", what);
    } else {
        snprintf(text, sizeof text, "%s: status %d %s", what, status, path);
    }
    kuex_set_text(g_doc, "shell-out", text);
}

/* Manual: the three file dialogs are modal. */
static void on_open(KuiEvent* e, void* u) {
    static char path[32768];
    KuiCFileDialogOptions options;
    size_t length = 0;
    (void)e;
    (void)u;
    memset(&options, 0, sizeof options);
    options.title = "Open a page";
    options.filters = k_filters;
    options.filter_count = 2;
    path[0] = '\0';
    show_path("open", kui_platform_open_file_dialog(&options, path, sizeof path, &length), path);
}

static void on_save(KuiEvent* e, void* u) {
    static char path[32768];
    KuiCFileDialogOptions options;
    size_t length = 0;
    (void)e;
    (void)u;
    memset(&options, 0, sizeof options);
    options.title = "Save the page as";
    options.default_path = "tour.html";
    options.filters = k_filters;
    options.filter_count = 2;
    path[0] = '\0';
    show_path("save", kui_platform_save_file_dialog(&options, path, sizeof path, &length), path);
}

static void on_folder(KuiEvent* e, void* u) {
    static char path[32768];
    size_t length = 0;
    (void)e;
    (void)u;
    path[0] = '\0';
    show_path("folder",
              kui_platform_select_folder_dialog("Pick a folder", 0, path, sizeof path, &length), path);
}

/* Manual: opens the system browser. */
static void on_url(KuiEvent* e, void* u) {
    char text[64];
    (void)e;
    (void)u;
    snprintf(text, sizeof text, "open_url: status %d",
             kui_platform_open_url("https://example.com/", 0));
    kuex_set_text(g_doc, "shell-out", text);
}

static void on_f12_on(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kuex_set_text(g_doc, "shell-out", kui_platform_set_f12_devtools_enabled(1) == KUI_OK
                                          ? "F12 enabled: F12 opens DevTools in the browser"
                                          : "set_f12_devtools_enabled failed");
}

static void on_f12_off(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kuex_set_text(g_doc, "shell-out", kui_platform_set_f12_devtools_enabled(0) == KUI_OK
                                          ? "F12 disabled" : "set_f12_devtools_enabled failed");
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    KuiTimerId timer = 0;
    int failed = 0;
    g_doc = host->document;
    g_window = host->window;

    check_clock_and_timers();
    check_event_loop();
    check_clipboard();
    check_monitors();
    check_f12();
    check_diagnostics();
    kui_platform_set_timeout(finalize, NULL, 700, &timer);

    failed |= kuex_on(g_doc, "btn-timeout", "click", on_timeout_btn, NULL);
    failed |= kuex_on(g_doc, "btn-interval", "click", on_interval_btn, NULL);
    failed |= kuex_on(g_doc, "btn-cancel", "click", on_cancel_btn, NULL);
    failed |= kuex_on(g_doc, "btn-idle", "click", on_idle_btn, NULL);
    failed |= kuex_on(g_doc, "btn-post", "click", on_post_btn, NULL);
    failed |= kuex_on(g_doc, "btn-poll", "click", on_poll_btn, NULL);
    failed |= kuex_on(g_doc, "btn-wait", "click", on_wait_btn, NULL);
    failed |= kuex_on(g_doc, "btn-clip-read", "click", on_clip_read, NULL);
    failed |= kuex_on(g_doc, "btn-clip-trip", "click", on_clip_trip, NULL);
    failed |= kuex_on(g_doc, "btn-mon-list", "click", on_mon_list, NULL);
    failed |= kuex_on(g_doc, "btn-mon-window", "click", on_mon_window, NULL);
    failed |= kuex_on(g_doc, "btn-msg", "click", on_msg, NULL);
    failed |= kuex_on(g_doc, "btn-open", "click", on_open, NULL);
    failed |= kuex_on(g_doc, "btn-save", "click", on_save, NULL);
    failed |= kuex_on(g_doc, "btn-folder", "click", on_folder, NULL);
    failed |= kuex_on(g_doc, "btn-url", "click", on_url, NULL);
    failed |= kuex_on(g_doc, "btn-f12-on", "click", on_f12_on, NULL);
    failed |= kuex_on(g_doc, "btn-f12-off", "click", on_f12_off, NULL);
    return failed;
}
