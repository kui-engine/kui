// =============================================================================
// kui_c_style.cpp — inline style declaration and laid-out geometry
// =============================================================================
#include "kui_c_internal.h"

#include <kui/core/rect.h>
#include <kui/css/css_value.h>
#include <kui/dom/element.h>

using kui_c::fromHandle;
using kui_c::str;
using kui_c::view;
using kui_c::writeString;

namespace {

inline kui::Element* el(KuiElement* h) {
    return fromHandle<kui::Element>(h);
}

// Element::style() lazily creates the declaration, so it is non-null for any
// live element; the check is here so a future change cannot turn into a crash.
inline kui::CSSStyleDeclaration* decl(KuiElement* h) {
    return el(h)->style();
}

// Geometry accessors all take four optional out-pointers. Rejecting the all-NULL
// call keeps a no-op from looking like success.
inline KuiStatus write4(float* a, float* b, float* c, float* d,
                        float va, float vb, float vc, float vd) {
    if (!a && !b && !c && !d) return KUI_ERR_INVALID_ARG;
    if (a) *a = va;
    if (b) *b = vb;
    if (c) *c = vc;
    if (d) *d = vd;
    return KUI_OK;
}

}  // namespace

extern "C" {

// --- Inline style ------------------------------------------------------------

KuiStatus kui_element_set_style_property(KuiElement* e, const char* prop, size_t prop_len,
                                         const char* value, size_t value_len,
                                         int important) {
    if (!e || !prop) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::CSSStyleDeclaration* d = decl(e);
        if (!d) return KUI_ERR_INTERNAL;
        d->setProperty(str(prop, prop_len), str(value, value_len), important != 0);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_get_style_property(KuiElement* e, const char* prop, size_t prop_len,
                                         char* buf, size_t buf_capacity, size_t* out_len) {
    if (!e || !prop) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::CSSStyleDeclaration* d = decl(e);
        if (!d) return KUI_ERR_INTERNAL;
        // getPropertyValue takes StringView -> no allocation for the lookup.
        const kui::CSSValue v = d->getPropertyValue(view(prop, prop_len));
        // An unset property yields an Invalid value, which is how absence is
        // distinguished from a property legitimately set to an empty value.
        if (!v.isValid()) return KUI_ERR_NOT_FOUND;
        const kui::String s = v.serialize();
        return writeString(s, buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_element_remove_style_property(KuiElement* e, const char* prop, size_t prop_len) {
    if (!e || !prop) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::CSSStyleDeclaration* d = decl(e);
        if (!d) return KUI_ERR_INTERNAL;
        d->removeProperty(str(prop, prop_len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_style_property_is_important(KuiElement* e, const char* prop, size_t prop_len,
                                                 int* out_important) {
    if (!e || !prop || !out_important) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::CSSStyleDeclaration* d = decl(e);
        if (!d) return KUI_ERR_INTERNAL;
        if (!d->getPropertyValue(view(prop, prop_len)).isValid()) return KUI_ERR_NOT_FOUND;
        *out_important = d->isImportant(view(prop, prop_len)) ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_get_style_text(KuiElement* e, char* buf, size_t buf_capacity,
                                     size_t* out_len) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::CSSStyleDeclaration* d = decl(e);
        if (!d) return KUI_ERR_INTERNAL;
        const kui::String text = d->cssText();
        return writeString(text, buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_element_set_style_text(KuiElement* e, const char* css, size_t css_len) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::CSSStyleDeclaration* d = decl(e);
        if (!d) return KUI_ERR_INTERNAL;
        d->setCssText(str(css, css_len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_get_style_property_count(KuiElement* e, size_t* out_count) {
    if (!e || !out_count) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::CSSStyleDeclaration* d = decl(e);
        if (!d) return KUI_ERR_INTERNAL;
        *out_count = static_cast<size_t>(d->length());
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_get_style_property_name(KuiElement* e, size_t index,
                                              char* buf, size_t buf_capacity, size_t* out_len) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::CSSStyleDeclaration* d = decl(e);
        if (!d) return KUI_ERR_INTERNAL;
        // Bounds-check here rather than relying on item()'s behaviour: an
        // out-of-range index is a caller error with a well-defined answer.
        if (index >= static_cast<size_t>(d->length())) return KUI_ERR_NOT_FOUND;
        const kui::String name = d->item(static_cast<kui::u32>(index));
        return writeString(name, buf, buf_capacity, out_len);
    KUI_C_END
}

// --- Geometry ----------------------------------------------------------------

KuiStatus kui_element_get_bounding_rect(KuiElement* e, KuiRect* out) {
    if (!e || !out) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        // kui::Rect is {f32 x, y, width, height} and KuiRect is
        // {float x, y, width, height} -- same members in the same order, so the
        // copy below is field-by-field on purpose rather than a memcpy: it stays
        // correct if either struct ever gains a member.
        const kui::Rect r = el(e)->boundingClientRect();
        out->x      = r.x;
        out->y      = r.y;
        out->width  = r.width;
        out->height = r.height;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_get_client_metrics(KuiElement* e, float* out_left, float* out_top,
                                        float* out_width, float* out_height) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::Element* p = el(e);
        return write4(out_left, out_top, out_width, out_height,
                      p->clientLeft(), p->clientTop(), p->clientWidth(), p->clientHeight());
    KUI_C_END
}

KuiStatus kui_element_get_offset_metrics(KuiElement* e, float* out_left, float* out_top,
                                        float* out_width, float* out_height) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::Element* p = el(e);
        return write4(out_left, out_top, out_width, out_height,
                      p->offsetLeft(), p->offsetTop(), p->offsetWidth(), p->offsetHeight());
    KUI_C_END
}

KuiStatus kui_element_get_scroll_metrics(KuiElement* e, float* out_left, float* out_top,
                                        float* out_width, float* out_height) {
    if (!e) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::Element* p = el(e);
        return write4(out_left, out_top, out_width, out_height,
                      p->scrollLeft(), p->scrollTop(), p->scrollWidth(), p->scrollHeight());
    KUI_C_END
}

}  // extern "C"
