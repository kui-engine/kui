/* Copied by scripts/generate_c_examples.py from examples/c/ports/native_platform_01_window_resize.c; edit that file, not this copy. */
/* Host logic of examples/native/platform/01_window_resize/main.cpp: #size shows the live client
 * size, once at start and again on every window resize. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;

static void update_size(uint32_t width, uint32_t height, void* user_data) {
    char text[96];
    (void)user_data;
    snprintf(text, sizeof text, "Live client size: %u × %u CSS px",
             (unsigned)width, (unsigned)height);
    kuex_set_text(g_doc, "size", text);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    int width = 0;
    int height = 0;
    g_doc = host->document;
    if (kui_window_get_size(host->window, &width, &height) != KUI_OK) return 1;
    update_size((uint32_t)width, (uint32_t)height, NULL);
    return kui_window_on_resize(host->window, update_size, NULL) == KUI_OK ? 0 : 1;
}
