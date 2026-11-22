// =============================================================================
// kui_c_element.cpp — Element lifetime, attributes, text content
// =============================================================================
#include "kui_c_internal.h"

#include <kui/dom/element.h>
#include <kui/dom/node_list.h>

using kui_c::fromHandle;
using kui_c::str;
using kui_c::toHandle;
using kui_c::view;
using kui_c::writeString;

namespace {

inline kui::Element* el(KuiElement* h) {
    return fromHandle<kui::Element>(h);
}

inline KuiStatus yieldElement(kui::Element* value, KuiElement** out) {
    if (!value) return KUI_ERR_NOT_FOUND;
    value->ref();
    *out = toHandle<KuiElement>(value);
    return KUI_OK;
}

inline KuiStatus yieldElements(const kui::RefPtr<kui::NodeList>& nodes,
                               KuiElement** out_buf, size_t buf_capacity,
                               size_t* out_count) {
    if (!out_count) return KUI_ERR_INVALID_ARG;
    const size_t count = nodes ? static_cast<size_t>(nodes->length()) : 0;
    *out_count = count;
    if (!out_buf || buf_capacity == 0)
        return out_buf ? (count == 0 ? KUI_OK : KUI_ERR_BUFFER_TOO_SMALL) : KUI_OK;
    if (buf_capacity < count) return KUI_ERR_BUFFER_TOO_SMALL;
    for (size_t i = 0; i < count; ++i) {
        auto* item = dynamic_cast<kui::Element*>(nodes->item(static_cast<kui::u32>(i)));
        if (!item) {
            for (size_t j = 0; j < i; ++j)
                fromHandle<kui::Element>(out_buf[j])->deref();
            return KUI_ERR_INTERNAL;
        }
        item->ref();
        out_buf[i] = toHandle<KuiElement>(item);
    }
    return KUI_OK;
}

}  // namespace

extern "C" {

KuiStatus kui_element_retain(KuiElement* e) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->ref();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_release(KuiElement* e) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->deref();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_get_tag_name(KuiElement* e, char* buf, size_t buf_capacity,
                                   size_t* out_len) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        // tagName() returns `const String&` -- viewed in place, no copy.
        return writeString(el(e)->tagName(), buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_element_get_id(KuiElement* e, char* buf, size_t buf_capacity,
                             size_t* out_len) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        // id() returns by value; the temporary must outlive writeString.
        const kui::String id = el(e)->id();
        return writeString(id, buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_element_get_class_name(KuiElement* e, char* buf, size_t buf_capacity,
                                     size_t* out_len) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const kui::String value = el(e)->className();
        return writeString(value, buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_element_set_class_name(KuiElement* e, const char* value, size_t value_len) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->setClassName(str(value, value_len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_set_attribute(KuiElement* e, const char* name, size_t name_len,
                                    const char* value, size_t value_len) {
    if (!e || !name) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        // setAttribute takes two `const String&` -- two constructions, dictated
        // by the C++ signature.
        el(e)->setAttribute(str(name, name_len), str(value, value_len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_get_attribute(KuiElement* e, const char* name, size_t name_len,
                                    char* buf, size_t buf_capacity, size_t* out_len) {
    if (!e || !name) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        // Absence and empty-string are indistinguishable in getAttribute's
        // return, so ask hasAttribute first and report absence properly.
        if (!el(e)->hasAttribute(view(name, name_len))) return KUI_ERR_NOT_FOUND;
        const kui::String v = el(e)->getAttribute(view(name, name_len));
        return writeString(v, buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_element_start_momentum_scroll(KuiElement* e, float velocity_x, float velocity_y) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->startMomentumScroll(velocity_x, velocity_y);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_scroll_into_view(KuiElement* e, int align_to_top) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->scrollIntoView(align_to_top != 0);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_has_attribute(KuiElement* e, const char* name, size_t name_len,
                                    int* out_has) {
    if (!e || !name || !out_has) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        // hasAttribute takes StringView -> no allocation.
        *out_has = el(e)->hasAttribute(view(name, name_len)) ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_remove_attribute(KuiElement* e, const char* name, size_t name_len) {
    if (!e || !name) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->removeAttribute(str(name, name_len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_set_text_content(KuiElement* e, const char* text, size_t text_len) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->setTextContent(str(text, text_len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_get_text_content(KuiElement* e, char* buf, size_t buf_capacity,
                                       size_t* out_len) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const kui::String text = el(e)->textContent();
        return writeString(text, buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_element_set_inner_html(KuiElement* e, const char* html, size_t html_len) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->setInnerHTML(str(html, html_len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_get_inner_html(KuiElement* e, char* buf, size_t buf_capacity,
                                     size_t* out_len) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const kui::String html = el(e)->innerHTML();
        return writeString(html, buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_element_class_list_add(KuiElement* e, const char* token, size_t len) {
    if (!e || !token) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->addClass(str(token, len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_class_list_remove(KuiElement* e, const char* token, size_t len) {
    if (!e || !token) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->removeClass(str(token, len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_class_list_toggle(KuiElement* e, const char* token, size_t len) {
    if (!e || !token) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->toggleClass(str(token, len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_class_list_contains(KuiElement* e, const char* token, size_t len,
                                          int* out_contains) {
    if (!e || !token || !out_contains) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_contains = el(e)->hasClass(view(token, len)) ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_query_selector(KuiElement* e, const char* sel, size_t sel_len,
                                     KuiElement** out) {
    if (!e || !sel || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        return yieldElement(el(e)->querySelector(view(sel, sel_len)), out);
    KUI_C_END
}

KuiStatus kui_element_query_selector_all(KuiElement* e, const char* sel, size_t sel_len,
                                         KuiElement** out_buf, size_t buf_capacity,
                                         size_t* out_count) {
    if (!e || !sel || !out_count) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        return yieldElements(el(e)->querySelectorAll(view(sel, sel_len)), out_buf,
                             buf_capacity, out_count);
    KUI_C_END
}

KuiStatus kui_element_parent(KuiElement* e, KuiElement** out) {
    if (!e || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        return yieldElement(el(e)->parentElement(), out);
    KUI_C_END
}

KuiStatus kui_element_first_child(KuiElement* e, KuiElement** out) {
    if (!e || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        return yieldElement(el(e)->firstElementChild(), out);
    KUI_C_END
}

KuiStatus kui_element_last_child(KuiElement* e, KuiElement** out) {
    if (!e || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        return yieldElement(el(e)->lastElementChild(), out);
    KUI_C_END
}

KuiStatus kui_element_next_sibling(KuiElement* e, KuiElement** out) {
    if (!e || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        return yieldElement(el(e)->nextElementSibling(), out);
    KUI_C_END
}

KuiStatus kui_element_previous_sibling(KuiElement* e, KuiElement** out) {
    if (!e || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        return yieldElement(el(e)->previousElementSibling(), out);
    KUI_C_END
}

KuiStatus kui_element_child_count(KuiElement* e, size_t* out_count) {
    if (!e || !out_count) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_count = static_cast<size_t>(el(e)->childElementCount());
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_append_child(KuiElement* parent, KuiElement* child) {
    if (!parent || !child) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        // The native appendChild does not check: appending an ancestor (or the node
        // itself) under its own subtree would turn the tree into a cycle.
        for (const kui::Node* node = el(parent); node; node = node->parentNode())
            if (node == el(child)) return KUI_ERR_INVALID_ARG;
        el(parent)->appendChild(kui::RefPtr<kui::Node>(el(child)));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_remove_child(KuiElement* parent, KuiElement* child) {
    if (!parent || !child) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto removed = el(parent)->removeChild(el(child));
        return removed ? KUI_OK : KUI_ERR_NOT_FOUND;
    KUI_C_END
}

KuiStatus kui_element_remove(KuiElement* e) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->remove();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_set_scroll_position(KuiElement* e, float left, float top) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->scrollTo(left, top);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_scroll_by(KuiElement* e, float dx, float dy) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->scrollBy(dx, dy);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_focus(KuiElement* e) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->focus();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_blur(KuiElement* e) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        el(e)->blur();
        return KUI_OK;
    KUI_C_END
}

}  // extern "C"
