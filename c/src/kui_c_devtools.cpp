// =============================================================================
// kui_c_devtools.cpp — in-process DevTools automation over the stable C ABI
// =============================================================================
#include "kui_c_internal.h"

#include <kui/devtools/devtools_server.h>
#include <kui/dom/document.h>
#include <kui/dom/element.h>
#include <kui/event/event_dispatcher.h>

using kui_c::fromHandle;
using kui_c::str;
using kui_c::writeString;

namespace {

inline kui::Document* doc(KuiDocument* handle) {
    return fromHandle<kui::Document>(handle);
}

inline bool modifiers(int value, bool& ctrl, bool& shift, bool& alt, bool& meta) {
    if ((value & ~15) != 0) return false;
    alt = (value & 1) != 0;
    ctrl = (value & 2) != 0;
    meta = (value & 4) != 0;
    shift = (value & 8) != 0;
    return true;
}

inline kui::MouseButton mouseButton(KuiCMouseButton button, bool& valid) {
    valid = true;
    switch (button) {
        case KUI_C_MOUSE_BUTTON_NONE: return kui::MouseButton::None;
        case KUI_C_MOUSE_BUTTON_LEFT: return kui::MouseButton::Left;
        case KUI_C_MOUSE_BUTTON_MIDDLE: return kui::MouseButton::Middle;
        case KUI_C_MOUSE_BUTTON_RIGHT: return kui::MouseButton::Right;
    }
    valid = false;
    return kui::MouseButton::None;
}

inline void writeQuad(const kui::Quad& quad, float* out) {
    out[0] = quad.p1.x; out[1] = quad.p1.y;
    out[2] = quad.p2.x; out[3] = quad.p2.y;
    out[4] = quad.p3.x; out[5] = quad.p3.y;
    out[6] = quad.p4.x; out[7] = quad.p4.y;
}

inline kui::Quad firstQuad(kui::Element* element, kui::Element::CSSBoxType type) {
    const auto quads = element->getBoxQuads(type);
    return quads.empty() ? kui::Quad::fromRect(element->boundingClientRect()) : quads.front();
}

}  // namespace

extern "C" {

KuiStatus kui_devtools_dispatch_mouse_event(KuiDocument* d, KuiCMouseEventType type,
                                             float x, float y, KuiCMouseButton button,
                                             int modifier_bits, int click_count,
                                             float delta_x, float delta_y) {
    if (!d || click_count < 0) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* dispatcher = doc(d)->eventDispatcher();
        if (!dispatcher) return KUI_ERR_NOT_FOUND;
        bool ctrl = false, shift = false, alt = false, meta = false;
        if (!modifiers(modifier_bits, ctrl, shift, alt, meta)) return KUI_ERR_INVALID_ARG;
        bool valid_button = false;
        const kui::MouseButton native_button = mouseButton(button, valid_button);
        if (!valid_button) return KUI_ERR_INVALID_ARG;
        const kui::u8 buttons = native_button == kui::MouseButton::Left ? 1u
                              : native_button == kui::MouseButton::Right ? 2u : 0u;

        switch (type) {
            case KUI_C_MOUSE_MOVED:
                dispatcher->handleMouseMove(x, y, buttons, ctrl, shift, alt, meta);
                break;
            case KUI_C_MOUSE_PRESSED:
                dispatcher->handleMouseDown(x, y, native_button, buttons,
                                            ctrl, shift, alt, meta);
                break;
            case KUI_C_MOUSE_RELEASED:
                dispatcher->handleMouseUp(x, y, native_button, buttons,
                                          ctrl, shift, alt, meta);
                break;
            case KUI_C_MOUSE_WHEEL:
                dispatcher->handleMouseWheel(x, y, delta_x, delta_y,
                                             ctrl, shift, alt, meta);
                break;
            default:
                return KUI_ERR_INVALID_ARG;
        }
        doc(d)->scheduleRepaint();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_devtools_dispatch_key_event(KuiDocument* d, KuiCKeyEventType type,
                                          const char* key, size_t key_len,
                                          const char* code, size_t code_len,
                                          int modifier_bits,
                                          const char* text, size_t text_len,
                                          int is_auto_repeat) {
    if (!d || !key) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* dispatcher = doc(d)->eventDispatcher();
        if (!dispatcher) return KUI_ERR_NOT_FOUND;
        bool ctrl = false, shift = false, alt = false, meta = false;
        if (!modifiers(modifier_bits, ctrl, shift, alt, meta)) return KUI_ERR_INVALID_ARG;
        const kui::String key_value = str(key, key_len);
        const kui::String code_value = code ? str(code, code_len) : key_value;
        switch (type) {
            case KUI_C_KEY_DOWN:
                dispatcher->handleKeyDown(key_value, code_value, ctrl, shift, alt, meta,
                                          is_auto_repeat != 0);
                break;
            case KUI_C_KEY_UP:
                dispatcher->handleKeyUp(key_value, code_value, ctrl, shift, alt, meta);
                break;
            case KUI_C_KEY_CHAR: {
                const kui::String value = text ? str(text, text_len) : key_value;
                if (!value.empty()) dispatcher->handleTextInput(value);
                break;
            }
            default:
                return KUI_ERR_INVALID_ARG;
        }
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_devtools_get_box_model(KuiDocument* d, const char* selector,
                                     float out_quads[32], float* out_width,
                                     float* out_height) {
    if (!d || !selector || !out_quads || !out_width || !out_height)
        return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::Element* element = doc(d)->querySelector(selector);
        if (!element) return KUI_ERR_NOT_FOUND;
        const kui::Quad content = firstQuad(element, kui::Element::CSSBoxType::Content);
        const kui::Quad padding = firstQuad(element, kui::Element::CSSBoxType::Padding);
        const kui::Quad border = firstQuad(element, kui::Element::CSSBoxType::Border);
        const kui::Quad margin = firstQuad(element, kui::Element::CSSBoxType::Margin);
        writeQuad(content, out_quads);
        writeQuad(padding, out_quads + 8);
        writeQuad(border, out_quads + 16);
        writeQuad(margin, out_quads + 24);
        const kui::Rect content_bounds = content.getBounds();
        *out_width = content_bounds.width;
        *out_height = content_bounds.height;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_devtools_dispatch_command(KuiDocument* d, const char* json_command,
                                        char* buf, size_t buf_capacity,
                                        size_t* out_len) {
    if (!d || !json_command) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::DevToolsServer server;
        server.attach(doc(d));
        const std::string response = server.dispatchCommandForTest(json_command);
        server.detach();
        return writeString(response, buf, buf_capacity, out_len);
    KUI_C_END
}

}  // extern "C"
