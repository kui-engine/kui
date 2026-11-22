/* Host logic of examples/native/component/11_anchored_popup/main.cpp: each .anchor button shows
 * #popup next to itself with its data-place placement (kui_element_position_relative_to, offset
 * 6, C++ defaults otherwise); clicking the same button again, or anywhere outside the anchors
 * and the popup, hides it. */
#include "kui_c_example_dom.h"

static KuiDocument* g_doc;
static char g_open_anchor[32]; /* id of the anchor the popup is open for, "" when hidden */

static void hide_popup(void) {
    kuex_set_style(g_doc, "popup", "display", "none");
    g_open_anchor[0] = '\0';
}

static void on_anchor(KuiEvent* event, void* user_data) {
    const char* anchor_id = (const char*)user_data;
    char place[64];
    KuiElement* popup;
    KuiElement* anchor;
    KuiCPositioningOptions options = {1, 1, 6.0f, 8.0f}; /* C++ defaults + offset 6 */
    (void)event;
    if (strcmp(g_open_anchor, anchor_id) == 0) {
        hide_popup();
        return;
    }
    kuex_get_attr(g_doc, anchor_id, "data-place", place, sizeof place);
    kuex_set_text(g_doc, "pp-place", place);
    /* Show first: positioning is a no-op while the popup is display:none. */
    kuex_set_style(g_doc, "popup", "display", "block");
    popup = kuex_by_id(g_doc, "popup");
    anchor = kuex_by_id(g_doc, anchor_id);
    if (popup && anchor) kui_element_position_relative_to(popup, anchor, place, 0, &options);
    if (anchor) kui_element_release(anchor);
    if (popup) kui_element_release(popup);
    snprintf(g_open_anchor, sizeof g_open_anchor, "%s", anchor_id);
}

/* Clicks that reach the root close the popup unless they hit an anchor or the popup itself. */
static void on_root_click(KuiEvent* event, void* user_data) {
    KuiElement* node = NULL;
    (void)user_data;
    if (g_open_anchor[0] == '\0') return;
    /* A non-element target (KUI_ERR_NOT_FOUND) cannot be walked; leave the popup alone. */
    if (kui_event_get_target(event, &node) != KUI_OK || !node) return;
    while (node) {
        char id[32];
        size_t length = 0;
        int is_anchor = 0;
        KuiElement* parent = NULL;
        id[0] = '\0';
        kui_element_get_id(node, id, sizeof id, &length);
        kui_element_class_list_contains(node, "anchor", 0, &is_anchor);
        if (strcmp(id, "popup") == 0 || is_anchor) {
            kui_element_release(node);
            return;
        }
        if (kui_element_parent(node, &parent) != KUI_OK) parent = NULL;
        kui_element_release(node);
        node = parent;
    }
    hide_popup();
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    static const char* const anchors[] = {"a-bottom", "a-top", "a-right", "a-left", "a-corner"};
    KuiElement* root = NULL;
    KuiStatus status;
    size_t i;
    g_doc = host->document;
    for (i = 0; i < sizeof anchors / sizeof anchors[0]; ++i) {
        KuiElement* button = kuex_by_id(g_doc, anchors[i]); /* the original skips missing ones */
        if (!button) continue;
        kui_element_release(button);
        if (kuex_on(g_doc, anchors[i], "click", on_anchor, (void*)anchors[i]) != 0) return 1;
    }
    if (kui_document_root(g_doc, &root) != KUI_OK || !root) return 1;
    status = kui_element_add_event_listener(root, "click", 0, on_root_click, NULL, NULL);
    kui_element_release(root);
    return status == KUI_OK ? 0 : 1;
}
