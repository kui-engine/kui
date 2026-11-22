/* Copied by scripts/generate_c_examples.py from examples/c/ports/native_platform_02_dpi_scaling.c; edit that file, not this copy. */
/* Host logic of examples/native/platform/02_dpi_scaling/main.cpp: writes the window's device pixel
 * ratio into #dpr (std::to_string formatting, "%f") and the truncated percentage into #scale. */
#include "kui_c_example_dom.h"

int kui_c_example_ready(const KuiCExampleHost* host) {
    float dpr = 1.0f;
    char text[32];
    if (kui_window_get_device_pixel_ratio(host->window, &dpr) != KUI_OK) return 1;
    snprintf(text, sizeof text, "%f", dpr);
    kuex_set_text(host->document, "dpr", text);
    snprintf(text, sizeof text, "%d%%", (int)(dpr * 100.0f));
    kuex_set_text(host->document, "scale", text);
    return 0;
}
