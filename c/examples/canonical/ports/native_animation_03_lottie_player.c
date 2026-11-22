/* Host logic of examples/native/animation/03_lottie_player/main.cpp: the runner's setup_lottie
 * already points <lottie-player id="lottie"> at the bundled native_animation/pulse.json and plays
 * it; this reports the outcome in #status the way the original does. The runner only sets src
 * when it found the file, so an empty src means "not found". */
#include "kui_c_example_dom.h"

int kui_c_example_ready(const KuiCExampleHost* host) {
    char src[1024];
    char status[160];
    int complete = 0;
    float width = 0.0f, height = 0.0f;
    KuiElement* player = kuex_by_id(host->document, "lottie");

    kuex_get_attr(host->document, "lottie", "src", src, sizeof src);
    if (!player || src[0] == '\0') {
        snprintf(status, sizeof status, "Bundled pulse.json was not found.");
    } else {
        kui_lottie_get_complete(player, &complete);
        kui_lottie_get_natural_size(player, &width, &height);
        if (complete)
            snprintf(status, sizeof status, "Playing bundled pulse.json \xC2\xB7 %u \xC3\x97 %u",
                     (unsigned)width, (unsigned)height);
        else
            snprintf(status, sizeof status, "Lottie decoder rejected pulse.json");
    }
    if (player) kui_element_release(player);
    kuex_set_text(host->document, "status", status);
    return 0;
}
