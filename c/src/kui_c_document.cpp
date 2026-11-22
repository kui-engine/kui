// =============================================================================
// kui_c_document.cpp — Document lifetime, HTML loading, DOM lookup
// =============================================================================
#include "kui_c_internal.h"

#include <kui/dom/document.h>
#include <kui/dom/element.h>
#include <kui/dom/node_list.h>
#include <kui/render/render_backend.h>

using kui_c::fromHandle;
using kui_c::toHandle;
using kui_c::str;
using kui_c::view;
using kui_c::writeString;

namespace {

inline kui::Document* doc(KuiDocument* h) {
    return fromHandle<kui::Document>(h);
}

// Shared tail for every lookup that yields an Element: takes the reference the
// caller is documented to receive, so the element cannot be destroyed while the
// handle lives. Returns KUI_ERR_NOT_FOUND for a null result, which is what all
// the querying C++ methods signal absence with.
inline KuiStatus yieldElement(kui::Element* el, KuiElement** out) {
    if (!el) return KUI_ERR_NOT_FOUND;
    el->ref();
    *out = toHandle<KuiElement>(el);
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

KuiStatus kui_document_create(KuiDocument** out) {
    if (!out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        kui::RefPtr<kui::Document> d = kui::Document::create();
        if (!d) return KUI_ERR_OUT_OF_MEMORY;
        // release() hands the RefPtr's reference over to the handle rather than
        // dropping it, so the count stays at exactly 1 and belongs to the caller.
        *out = toHandle<KuiDocument>(d.release());
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_retain(KuiDocument* d) {
    if (!d) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        doc(d)->ref();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_release(KuiDocument* d) {
    // Rejecting NULL rather than treating it as a no-op: a double release shows
    // up as an error return instead of passing quietly.
    if (!d) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        doc(d)->deref();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_load_html(KuiDocument* d, const char* html, size_t html_len) {
    if (!d) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        // loadHTML takes `const String&`, so one construction is unavoidable
        // here -- that is the C++ signature, not an artefact of this layer.
        doc(d)->loadHTML(str(html, html_len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_load_html_file(KuiDocument* d, const char* path, size_t path_len) {
    if (!d || !path) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        doc(d)->loadFile(str(path, path_len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_add_stylesheet(KuiDocument* d, const char* css, size_t css_len) {
    if (!d || !css) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        doc(d)->addStylesheet(str(css, css_len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_clear_stylesheets(KuiDocument* d) {
    if (!d) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        while (!doc(d)->stylesheets().empty()) {
            if (!doc(d)->removeStylesheet(doc(d)->stylesheets().back().get()))
                return KUI_ERR_INTERNAL;
        }
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_root(KuiDocument* d, KuiElement** out) {
    if (!d || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        return yieldElement(doc(d)->documentElement(), out);
    KUI_C_END
}

KuiStatus kui_document_body(KuiDocument* d, KuiElement** out) {
    if (!d || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        return yieldElement(doc(d)->body(), out);
    KUI_C_END
}

KuiStatus kui_document_head(KuiDocument* d, KuiElement** out) {
    if (!d || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        return yieldElement(doc(d)->head(), out);
    KUI_C_END
}

KuiStatus kui_document_set_base_path(KuiDocument* d, const char* path, size_t path_len) {
    if (!d || !path) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        doc(d)->setBasePath(str(path, path_len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_get_base_path(KuiDocument* d, char* buf, size_t buf_capacity, size_t* out_len) {
    if (!d) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        return writeString(doc(d)->basePath(), buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_document_set_render_device_pixel_ratio(KuiDocument* d, float dpr) {
    if (!d || !(dpr > 0.0f)) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::RenderBackend* backend = doc(d)->renderBackend();
        if (!backend) return KUI_ERR_NOT_SUPPORTED;
        backend->setDevicePixelRatio(dpr);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_get_element_by_id(KuiDocument* d, const char* id, size_t id_len,
                                         KuiElement** out) {
    if (!d || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        // getElementById takes StringView -> the caller's bytes are used in
        // place, no allocation.
        return yieldElement(doc(d)->getElementById(view(id, id_len)), out);
    KUI_C_END
}

KuiStatus kui_document_query_selector(KuiDocument* d, const char* sel, size_t sel_len,
                                      KuiElement** out) {
    if (!d || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        return yieldElement(doc(d)->querySelector(view(sel, sel_len)), out);
    KUI_C_END
}

KuiStatus kui_document_query_selector_all(KuiDocument* d, const char* sel, size_t sel_len,
                                          KuiElement** out_buf, size_t buf_capacity,
                                          size_t* out_count) {
    if (!d || !sel || !out_count) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        return yieldElements(doc(d)->querySelectorAll(view(sel, sel_len)), out_buf,
                             buf_capacity, out_count);
    KUI_C_END
}

KuiStatus kui_document_get_elements_by_class_name(KuiDocument* d, const char* class_name,
                                                  size_t len, KuiElement** out_buf,
                                                  size_t buf_capacity, size_t* out_count) {
    if (!d || !class_name || !out_count) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        return yieldElements(doc(d)->getElementsByClassName(view(class_name, len)), out_buf,
                             buf_capacity, out_count);
    KUI_C_END
}

KuiStatus kui_document_get_elements_by_tag_name(KuiDocument* d, const char* tag_name,
                                                size_t len, KuiElement** out_buf,
                                                size_t buf_capacity, size_t* out_count) {
    if (!d || !tag_name || !out_count) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        return yieldElements(doc(d)->getElementsByTagName(view(tag_name, len)), out_buf,
                             buf_capacity, out_count);
    KUI_C_END
}

KuiStatus kui_document_create_element(KuiDocument* d, const char* tag, size_t tag_len,
                                      KuiElement** out) {
    if (!d || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        kui::RefPtr<kui::Element> el = doc(d)->createElement(str(tag, tag_len));
        if (!el) return KUI_ERR_INVALID_ARG;   // rejected tag name
        *out = toHandle<KuiElement>(el.release());
        return KUI_OK;
    KUI_C_END
}

}  // extern "C"
