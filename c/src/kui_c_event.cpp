// =============================================================================
// kui_c_event.cpp — listener registration and event accessors
// =============================================================================
#include "kui_c_internal.h"

#include <kui/dom/document.h>
#include <kui/dom/element.h>
#include <kui/event/event.h>
#include <kui/event/event_dispatcher.h>

using kui_c::fromHandle;
using kui_c::str;
using kui_c::toHandle;
using kui_c::view;
using kui_c::writeString;

namespace {

inline kui::Element* el(KuiElement* h) {
    return fromHandle<kui::Element>(h);
}

inline kui::Event* ev(KuiEvent* h) {
    return fromHandle<kui::Event>(h);
}

// Event::target() is a Node*, which may be a text node rather than an element.
// Reference the result the same way every other handle-yielding function does.
inline KuiStatus yieldElementFromNode(kui::Node* n, KuiElement** out) {
    if (!n || !n->isElement()) return KUI_ERR_NOT_FOUND;
    kui::Element* e = static_cast<kui::Element*>(n);
    e->ref();
    *out = toHandle<KuiElement>(e);
    return KUI_OK;
}

// Mouse and keyboard accessors are declared against KuiEvent, so the concrete
// type has to be recovered. dynamic_cast rather than inspecting the type string:
// the class hierarchy is the authority on which fields exist, and event names are
// not a reliable proxy (WheelEvent and PointerEvent both derive from MouseEvent).
inline kui::MouseEvent* asMouse(KuiEvent* h) {
    return dynamic_cast<kui::MouseEvent*>(ev(h));
}

inline kui::KeyboardEvent* asKey(KuiEvent* h) {
    return dynamic_cast<kui::KeyboardEvent*>(ev(h));
}

inline KuiStatus writeXY(float* x, float* y, float vx, float vy) {
    if (!x && !y) return KUI_ERR_INVALID_ARG;   // nothing to report
    if (x) *x = vx;
    if (y) *y = vy;
    return KUI_OK;
}

}  // namespace

extern "C" {

KuiStatus kui_element_add_event_listener(KuiElement* e, const char* event_name, size_t len,
                                         KuiEventListenerFn fn, void* user_data,
                                         KuiListenerId* out_id) {
    return kui_element_add_event_listener_ex(e, event_name, len, fn, user_data, 0, out_id);
}

KuiStatus kui_element_add_event_listener_ex(KuiElement* e, const char* event_name, size_t len,
                                            KuiEventListenerFn fn, void* user_data, int capture,
                                            KuiListenerId* out_id) {
    if (!e || !event_name || !fn) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        // The trampoline captures two plain pointers and nothing else -- 16 bytes,
        // trivially copyable, nothrow-movable. That matters: kui::Callback stores
        // callables up to 48 bytes inline and RELOCATES that storage with memcpy
        // (see kui/core/callback.h, which static_asserts against wrapping a
        // std::function for exactly this reason). Capturing anything with
        // non-trivial move semantics would be relying on undefined behaviour.
        //
        // Capturing an owning pointer (to a heap-allocated context struct) would
        // work for relocation but raise a harder question: who frees it when the
        // element is destroyed while listeners are still attached? Capturing only
        // POD means there is no ownership to settle.
        const kui::u32 id = el(e)->addEventListener(
            str(event_name, len),
            [fn, user_data](kui::Event& event) {
                // A C function pointer must not have an exception unwind through
                // it. Our own C API never throws, but a listener compiled as C++
                // might, so the boundary is enforced here too.
                try {
                    fn(toHandle<KuiEvent>(&event), user_data);
                } catch (...) {
                    // Swallowed deliberately: there is no channel to report an
                    // error to from inside dispatch, and letting it escape would
                    // unwind through KUI's dispatcher.
                }
            },
            capture != 0);
        if (out_id) *out_id = static_cast<KuiListenerId>(id);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_remove_event_listener(KuiElement* e, const char* event_name, size_t len,
                                           KuiListenerId id) {
    if (!e || !event_name) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->removeEventListener(str(event_name, len), static_cast<kui::u32>(id));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_remove_event_listeners(KuiElement* e, const char* event_name, size_t len) {
    if (!e || !event_name) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->removeEventListeners(str(event_name, len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_dispatch_event(KuiElement* e, const char* event_name, size_t len,
                                     int bubbles, int cancelable,
                                     int* out_not_canceled) {
    if (!e || !event_name) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::Element* target = el(e);
        kui::Document* doc = target->ownerDocument();
        if (!doc) return KUI_ERR_INVALID_ARG;          // detached element
        kui::EventDispatcher* disp = doc->eventDispatcher();
        if (!disp) return KUI_ERR_NOT_SUPPORTED;       // document without a dispatcher

        kui::Event event(str(event_name, len), bubbles != 0, cancelable != 0);
        const bool notCanceled = disp->dispatchEvent(target, event);
        if (out_not_canceled) *out_not_canceled = notCanceled ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

// --- Event accessors ---------------------------------------------------------

KuiStatus kui_event_get_type(KuiEvent* e, char* buf, size_t buf_capacity, size_t* out_len) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        return writeString(ev(e)->type(), buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_event_get_target(KuiEvent* e, KuiElement** out) {
    if (!e || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        return yieldElementFromNode(ev(e)->target(), out);
    KUI_C_END
}

KuiStatus kui_event_get_current_target(KuiEvent* e, KuiElement** out) {
    if (!e || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        return yieldElementFromNode(ev(e)->currentTarget(), out);
    KUI_C_END
}

KuiStatus kui_event_get_related_target(KuiEvent* e, KuiElement** out) {
    if (!e || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        kui::Element* related = nullptr;
        if (auto* mouse = asMouse(e)) related = mouse->relatedTarget();
        else if (auto* focus = dynamic_cast<kui::FocusEvent*>(ev(e))) related = focus->relatedTarget();
        return yieldElementFromNode(related, out);
    KUI_C_END
}

KuiStatus kui_event_prevent_default(KuiEvent* e) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        ev(e)->preventDefault();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_event_stop_propagation(KuiEvent* e) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        ev(e)->stopPropagation();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_event_get_flags(KuiEvent* e, int* out_bubbles, int* out_cancelable,
                              int* out_default_prevented) {
    if (!e) return KUI_ERR_INVALID_ARG;
    if (!out_bubbles && !out_cancelable && !out_default_prevented) {
        return KUI_ERR_INVALID_ARG;
    }
    KUI_C_BEGIN
        kui::Event* p = ev(e);
        if (out_bubbles)           *out_bubbles = p->bubbles() ? 1 : 0;
        if (out_cancelable)        *out_cancelable = p->cancelable() ? 1 : 0;
        if (out_default_prevented) *out_default_prevented = p->isDefaultPrevented() ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

// --- Mouse -------------------------------------------------------------------

KuiStatus kui_event_mouse_get_client_xy(KuiEvent* e, float* x, float* y) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::MouseEvent* m = asMouse(e);
        if (!m) return KUI_ERR_NOT_SUPPORTED;
        return writeXY(x, y, m->clientX(), m->clientY());
    KUI_C_END
}

KuiStatus kui_event_mouse_get_page_xy(KuiEvent* e, float* x, float* y) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::MouseEvent* m = asMouse(e);
        if (!m) return KUI_ERR_NOT_SUPPORTED;
        return writeXY(x, y, m->pageX(), m->pageY());
    KUI_C_END
}

KuiStatus kui_event_mouse_get_offset_xy(KuiEvent* e, float* x, float* y) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::MouseEvent* m = asMouse(e);
        if (!m) return KUI_ERR_NOT_SUPPORTED;
        return writeXY(x, y, m->offsetX(), m->offsetY());
    KUI_C_END
}

KuiStatus kui_event_wheel_get_delta(KuiEvent* e, float* out_delta_x, float* out_delta_y) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* wheel = dynamic_cast<kui::WheelEvent*>(ev(e));
        if (!wheel) return KUI_ERR_NOT_SUPPORTED;
        return writeXY(out_delta_x, out_delta_y, wheel->deltaX(), wheel->deltaY());
    KUI_C_END
}

KuiStatus kui_event_mouse_get_button(KuiEvent* e, int* out_button) {
    if (!e || !out_button) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::MouseEvent* m = asMouse(e);
        if (!m) return KUI_ERR_NOT_SUPPORTED;
        // kui::MouseButton is None=0, Left=1, Middle=2, Right=3, which is exactly
        // how KUI_C_MOUSE_BUTTON_* is numbered -- a straight cast, no mapping
        // table to drift out of sync. (Note this is NOT the W3C numbering the
        // extension ABI uses, where 0 means left; see the note on the enums.)
        *out_button = static_cast<int>(m->button());
        return KUI_OK;
    KUI_C_END
}

// --- Keyboard ----------------------------------------------------------------

KuiStatus kui_event_key_get_key(KuiEvent* e, char* buf, size_t buf_capacity, size_t* out_len) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::KeyboardEvent* k = asKey(e);
        if (!k) return KUI_ERR_NOT_SUPPORTED;
        return writeString(k->key(), buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_event_key_get_code(KuiEvent* e, char* buf, size_t buf_capacity, size_t* out_len) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::KeyboardEvent* k = asKey(e);
        if (!k) return KUI_ERR_NOT_SUPPORTED;
        return writeString(k->code(), buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_event_key_get_modifiers(KuiEvent* e, int* out_ctrl, int* out_shift,
                                      int* out_alt, int* out_meta) {
    if (!e) return KUI_ERR_INVALID_ARG;
    if (!out_ctrl && !out_shift && !out_alt && !out_meta) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::KeyboardEvent* k = asKey(e);
        if (!k) return KUI_ERR_NOT_SUPPORTED;
        if (out_ctrl)  *out_ctrl  = k->ctrlKey()  ? 1 : 0;
        if (out_shift) *out_shift = k->shiftKey() ? 1 : 0;
        if (out_alt)   *out_alt   = k->altKey()   ? 1 : 0;
        if (out_meta)  *out_meta  = k->metaKey()  ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

}  // extern "C"
