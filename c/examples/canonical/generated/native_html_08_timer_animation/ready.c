/* Copied by scripts/generate_c_examples.py from examples/c/ports/native_html_08_timer_animation.c; edit that file, not this copy. */
/* Host logic of examples/native/html/08_timer_animation/main.cpp: a 1 s interval writes the local
 * time into #clock (%H:%M:%S) and #date (%Y-%m-%d %A), sets #progress's width to sec*100/59 %
 * and counts "Ticks: N" in #counter. */
#include "kui_c_example_dom.h"

#include <time.h>

static KuiDocument* g_doc;
static int g_ticks;

static void on_tick(void* user_data) {
    time_t now = time(NULL);
    struct tm tm_buf;
    char time_text[16];
    char date_text[32];
    char value[32];
    (void)user_data;
#if defined(_WIN32)
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    strftime(time_text, sizeof time_text, "%H:%M:%S", &tm_buf);
    strftime(date_text, sizeof date_text, "%Y-%m-%d %A", &tm_buf);
    kuex_set_text(g_doc, "clock", time_text);
    kuex_set_text(g_doc, "date", date_text);

    snprintf(value, sizeof value, "%d%%", (tm_buf.tm_sec * 100) / 59);
    kuex_set_style(g_doc, "progress", "width", value);

    ++g_ticks;
    snprintf(value, sizeof value, "Ticks: %d", g_ticks);
    kuex_set_text(g_doc, "counter", value);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    KuiTimerId timer = 0;
    g_doc = host->document;
    return kui_platform_set_interval(on_tick, NULL, 1000, &timer) == KUI_OK ? 0 : 1;
}
