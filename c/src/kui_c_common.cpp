// =============================================================================
// kui_c_common.cpp — section 9 of kui_c.h: common window, DOM and platform
// operations added in 3.61.8 (window focus / cursor / fullscreen / frame,
// tree editing, selectors, serialization, pointer capture,
// message box, URL, clock, monitor lookup).
// =============================================================================
#include "kui_c_internal.h"

#include <kui/dom/document.h>
#include <kui/dom/element.h>
#include <kui/dom/text.h>
#include <kui/platform/platform.h>
#include <kui/platform/window.h>

using kui_c::fromHandle;
using kui_c::str;
using kui_c::toHandle;
using kui_c::view;
using kui_c::writeString;

namespace {

inline kui::Window* win(KuiWindow* value) { return fromHandle<kui::Window>(value); }
inline kui::Document* doc(KuiDocument* value) { return fromHandle<kui::Document>(value); }
inline kui::Element* el(KuiElement* value) { return fromHandle<kui::Element>(value); }

// Hand back a counted reference, or NOT_FOUND for null.
inline KuiStatus yieldElement(kui::Element* value, KuiElement** out) {
    if (!value) return KUI_ERR_NOT_FOUND;
    value->ref();
    *out = toHandle<KuiElement>(value);
    return KUI_OK;
}

// The native insertBefore / replaceChild only assert that the reference node is
// a child; a release build would then corrupt the sibling links. Inserting an
// ancestor under its own descendant would create a cycle. Both are caller
// errors that C must report instead.
inline bool isInclusiveAncestor(const kui::Node* candidate, const kui::Node* node) {
    for (const kui::Node* current = node; current; current = current->parentNode())
        if (current == candidate) return true;
    return false;
}

inline void writeMonitor(const kui::Platform::MonitorInfo& value, KuiCMonitorInfo* out) {
    out->bounds_x = value.bounds.x;
    out->bounds_y = value.bounds.y;
    out->bounds_width = value.bounds.width;
    out->bounds_height = value.bounds.height;
    out->work_x = value.workArea.x;
    out->work_y = value.workArea.y;
    out->work_width = value.workArea.width;
    out->work_height = value.workArea.height;
    out->scale = value.scale;
    out->is_primary = value.isPrimary ? 1 : 0;
}

}  // namespace

extern "C" {

// --- Window ------------------------------------------------------------------

KuiStatus kui_window_focus(KuiWindow* w) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->focus();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_has_focus(KuiWindow* w, int* out_focused) {
    if (!w || !out_focused) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_focused = win(w)->hasFocus() ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_set_cursor(KuiWindow* w, KuiCCursor cursor) {
    if (!w || cursor < KUI_C_CURSOR_AUTO || cursor > KUI_C_CURSOR_ROW_RESIZE)
        return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->setCursor(static_cast<kui::u8>(cursor));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_set_cursor_visible(KuiWindow* w, int visible) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->setCursorVisible(visible != 0);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_set_fullscreen(KuiWindow* w, int fullscreen) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->setFullscreen(fullscreen != 0);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_is_fullscreen(KuiWindow* w, int* out_fullscreen) {
    if (!w || !out_fullscreen) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_fullscreen = win(w)->isFullscreen() ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_set_frameless(KuiWindow* w, int frameless) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->setFrameless(frameless != 0);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_set_frameless_shadow(KuiWindow* w, int shadow) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->setFramelessShadow(shadow != 0);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_get_frame_state(KuiWindow* w, int* out_frameless, int* out_shadow,
                                     int* out_sizeable) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        if (out_frameless) *out_frameless = win(w)->isFrameless() ? 1 : 0;
        if (out_shadow) *out_shadow = win(w)->hasFramelessShadow() ? 1 : 0;
        if (out_sizeable) *out_sizeable = win(w)->isFramelessSizeable() ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_set_frame_radii(KuiWindow* w, float top_left, float top_right,
                                     float bottom_right, float bottom_left) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->setFrameRadii(kui::WindowFrameRadii(top_left, top_right, bottom_right,
                                                    bottom_left));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_get_frame_radii(KuiWindow* w, float* out_top_left, float* out_top_right,
                                     float* out_bottom_right, float* out_bottom_left) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const auto radii = win(w)->frameRadii();
        if (!radii) return KUI_ERR_NOT_FOUND;
        if (out_top_left) *out_top_left = radii->topLeft;
        if (out_top_right) *out_top_right = radii->topRight;
        if (out_bottom_right) *out_bottom_right = radii->bottomRight;
        if (out_bottom_left) *out_bottom_left = radii->bottomLeft;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_set_ime_candidate_pos(KuiWindow* w, float x, float y) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->setImeCandidatePos(x, y);
        return KUI_OK;
    KUI_C_END
}

// --- DOM ---------------------------------------------------------------------

KuiStatus kui_element_insert_before(KuiElement* parent, KuiElement* child,
                                    KuiElement* ref_child) {
    if (!parent || !child || child == parent) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::Element* p = el(parent);
        kui::Element* c = el(child);
        if (ref_child && el(ref_child)->parentNode() != p) return KUI_ERR_NOT_FOUND;
        if (isInclusiveAncestor(c, p)) return KUI_ERR_INVALID_ARG;
        if (ref_child == child) return KUI_OK;  // already in place
        kui::Node* inserted =
            p->insertBefore(kui::RefPtr<kui::Node>(c), ref_child ? el(ref_child) : nullptr);
        return inserted ? KUI_OK : KUI_ERR_INTERNAL;
    KUI_C_END
}

KuiStatus kui_element_replace_child(KuiElement* parent, KuiElement* new_child,
                                    KuiElement* old_child) {
    if (!parent || !new_child || !old_child) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::Element* p = el(parent);
        kui::Element* replacement = el(new_child);
        kui::Element* old = el(old_child);
        if (old->parentNode() != p) return KUI_ERR_NOT_FOUND;
        if (replacement == old) return KUI_OK;
        if (isInclusiveAncestor(replacement, p)) return KUI_ERR_INVALID_ARG;
        kui::RefPtr<kui::Node> keepOld(old);  // removeChild drops the tree's reference
        return p->replaceChild(kui::RefPtr<kui::Node>(replacement), old) ? KUI_OK
                                                                         : KUI_ERR_INTERNAL;
    KUI_C_END
}

KuiStatus kui_element_clone(KuiElement* e, int deep, KuiElement** out) {
    if (!e || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        kui::RefPtr<kui::Node> copy = el(e)->cloneNode(deep != 0);
        auto* element = dynamic_cast<kui::Element*>(copy.get());
        if (!element) return KUI_ERR_INTERNAL;
        element->ref();
        *out = toHandle<KuiElement>(element);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_matches(KuiElement* e, const char* sel, size_t sel_len,
                              int* out_matches) {
    if (!e || !sel || !out_matches) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_matches = el(e)->matches(view(sel, sel_len)) ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_closest(KuiElement* e, const char* sel, size_t sel_len,
                              KuiElement** out) {
    if (!e || !sel || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        return yieldElement(el(e)->closest(view(sel, sel_len)), out);
    KUI_C_END
}

KuiStatus kui_element_get_outer_html(KuiElement* e, char* buf, size_t buf_capacity,
                                     size_t* out_len) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        return writeString(el(e)->outerHTML(), buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_element_get_computed_style_value(KuiElement* e, const char* name, size_t name_len,
                                               char* buf, size_t buf_capacity, size_t* out_len) {
    if (!e || !name) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        return writeString(el(e)->computedStylePropertyValue(view(name, name_len)), buf,
                           buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_element_insert_adjacent_html(KuiElement* e, const char* position,
                                           size_t position_len, const char* html,
                                           size_t html_len) {
    if (!e || !position || !html) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const kui::String where = str(position, position_len);
        if (where != "beforebegin" && where != "afterbegin" && where != "beforeend" &&
            where != "afterend")
            return KUI_ERR_INVALID_ARG;
        // beforebegin / afterend insert next to the element, which needs a parent.
        if ((where == "beforebegin" || where == "afterend") && !el(e)->parentNode())
            return KUI_ERR_NOT_SUPPORTED;
        el(e)->insertAdjacentHTML(where, str(html, html_len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_append_text(KuiElement* e, const char* text, size_t len) {
    if (!e || !text) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::Document* owner = el(e)->ownerDocument();
        if (!owner) return KUI_ERR_NOT_SUPPORTED;
        el(e)->appendChild(owner->createTextNode(str(text, len)));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_has_focus(KuiElement* e, int* out_focused) {
    if (!e || !out_focused) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_focused = el(e)->hasFocus() ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_set_pointer_capture(KuiElement* e, int pointer_id) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->setPointerCapture(pointer_id);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_release_pointer_capture(KuiElement* e, int pointer_id) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->releasePointerCapture(pointer_id);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_has_pointer_capture(KuiElement* e, int pointer_id, int* out_captured) {
    if (!e || !out_captured) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_captured = el(e)->hasPointerCapture(pointer_id) ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_element_from_point(KuiDocument* d, float x, float y, KuiElement** out) {
    if (!d || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        return yieldElement(doc(d)->elementFromPoint(x, y), out);
    KUI_C_END
}

// --- Platform ----------------------------------------------------------------

KuiStatus kui_platform_message_box(const char* title, size_t title_len, const char* message,
                                   size_t message_len, KuiCMessageBoxType type,
                                   KuiCMessageBoxButtons buttons,
                                   KuiCMessageBoxResult* out_result) {
    if (!message || type < KUI_C_MESSAGE_BOX_INFO || type > KUI_C_MESSAGE_BOX_QUESTION ||
        buttons < KUI_C_MESSAGE_BOX_OK || buttons > KUI_C_MESSAGE_BOX_YES_NO_CANCEL)
        return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const auto result = kui::Platform::messageBox(
            title ? str(title, title_len) : kui::String(), str(message, message_len),
            static_cast<kui::Platform::MsgBoxType>(type),
            static_cast<kui::Platform::MsgBoxButtons>(buttons));
        if (out_result) *out_result = static_cast<KuiCMessageBoxResult>(result);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_open_url(const char* url, size_t len) {
    if (!url) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::Platform::openUrl(str(url, len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_now(double* out_ms) {
    if (!out_ms) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_ms = kui::Platform::now();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_get_monitor_from_point(float x, float y, KuiCMonitorInfo* out_monitor,
                                              char* name_buf, size_t name_capacity,
                                              size_t* out_name_len) {
    if (!out_monitor) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const auto value = kui::Platform::monitorFromPoint(x, y);
        writeMonitor(value, out_monitor);
        return writeString(value.name, name_buf, name_capacity, out_name_len);
    KUI_C_END
}

KuiStatus kui_platform_get_monitor_from_window(void* native_window, KuiCMonitorInfo* out_monitor,
                                               char* name_buf, size_t name_capacity,
                                               size_t* out_name_len) {
    if (!native_window || !out_monitor) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const auto value = kui::Platform::monitorFromWindow(native_window);
        writeMonitor(value, out_monitor);
        return writeString(value.name, name_buf, name_capacity, out_name_len);
    KUI_C_END
}

}  // extern "C"
