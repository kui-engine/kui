/* Host logic of examples/native/showcase/07_music_player/main.cpp: #playBtn toggles its glyph
 * between play (U+25B6) and pause (U+23F8). No audio is involved in the original. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;
static int g_playing;

static void on_play(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    g_playing = !g_playing;
    kuex_set_text(g_doc, "playBtn", g_playing ? "\xE2\x8F\xB8" : "\xE2\x96\xB6");
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    return kuex_on(g_doc, "playBtn", "click", on_play, NULL);
}
