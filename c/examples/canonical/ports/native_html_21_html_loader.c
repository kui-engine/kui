/* Host logic of examples/native/html/21_html_loader/main.cpp: once the inline page is loaded,
 * #status reports it. */
#include "kui_c_example_dom.h"

int kui_c_example_ready(const KuiCExampleHost* host) {
    KuiElement* status = kuex_by_id(host->document, "status");
    if (!status) return 1;
    kui_element_set_text_content(status, "Inline HTML loaded successfully.", 0);
    kui_element_release(status);
    return 0;
}
