#ifndef KUI_C_EXAMPLE_DOM_H
#define KUI_C_EXAMPLE_DOM_H

/* Small helpers the hand-written example ports (examples/c/ports/<target>.c) share, so a port
 * reads like the C++ original: look an element up by id, set text, set a style property, listen
 * for an event. Everything is a thin wrapper over the public kui_c.h functions; a length of 0
 * means "NUL-terminated" in that API.
 *
 * Handles are looked up where they are used and released straight away (the way the C++
 * originals call doc->getElementById inside their handlers), so a port never has to keep an
 * element alive across the event loop. */

#include "kui_c_example_runtime.h"

#include <stdio.h>
#include <string.h>

/* Retained element or NULL. Release with kui_element_release. */
static inline KuiElement* kuex_by_id(KuiDocument* doc, const char* id) {
    KuiElement* element = NULL;
    if (kui_document_get_element_by_id(doc, id, 0, &element) != KUI_OK) return NULL;
    return element;
}

static inline void kuex_set_text(KuiDocument* doc, const char* id, const char* text) {
    KuiElement* element = kuex_by_id(doc, id);
    if (!element) return;
    kui_element_set_text_content(element, text, 0);
    kui_element_release(element);
}

static inline void kuex_set_class(KuiDocument* doc, const char* id, const char* class_name) {
    KuiElement* element = kuex_by_id(doc, id);
    if (!element) return;
    kui_element_set_class_name(element, class_name, 0);
    kui_element_release(element);
}

static inline void kuex_set_style(KuiDocument* doc, const char* id,
                                  const char* property, const char* value) {
    KuiElement* element = kuex_by_id(doc, id);
    if (!element) return;
    kui_element_set_style_property(element, property, 0, value, 0, 0);
    kui_element_release(element);
}

/* Copies an attribute into buf (always NUL-terminated); empty string when absent. */
static inline void kuex_get_attr(KuiDocument* doc, const char* id, const char* name,
                                 char* buf, size_t capacity) {
    KuiElement* element = kuex_by_id(doc, id);
    size_t length = 0;
    if (capacity == 0) return;
    buf[0] = '\0';
    if (!element) return;
    if (kui_element_get_attribute(element, name, 0, buf, capacity, &length) != KUI_OK) {
        buf[0] = '\0';
    }
    buf[capacity - 1] = '\0';
    kui_element_release(element);
}

/* Evaluates a Lua chunk and copies its first return value, as text, into buf (empty when the
 * chunk fails or returns nil / a non-text value). The public C ABI has no accessor for what a
 * user typed into an <input> (kui_element_get_attribute only reads the content attribute), so
 * a host reads `element.value` through the script engine. See TODO T-API-C-ABI-FORM-VALUE. */
static inline void kuex_eval_text(KuiScriptEngine* script, const char* code,
                                  char* buf, size_t capacity) {
    KuiCScriptValue value;
    int success = 0;
    if (capacity == 0) return;
    buf[0] = '\0';
    memset(&value, 0, sizeof value);
    if (kui_script_engine_eval_value(script, code, 0, "example", 0, &success, &value) != KUI_OK ||
        !success || value.type != KUI_C_SCRIPT_STRING || !value.string_value) {
        return;
    }
    snprintf(buf, capacity, "%s", value.string_value);
}

/* Current value of a form control (what the user typed), through kui_element_get_value; empty
 * when the element is missing or has no value state. */
static inline void kuex_get_value(KuiDocument* doc, const char* id, char* buf, size_t capacity) {
    KuiElement* element;
    if (capacity == 0) return;
    buf[0] = '\0';
    element = kuex_by_id(doc, id);
    if (!element) return;
    (void)kui_element_get_value(element, buf, capacity, NULL);
    kui_element_release(element);
}

/* Sets a form control's current value (no input / change event). */
static inline void kuex_set_value(KuiDocument* doc, const char* id, const char* value) {
    KuiElement* element = kuex_by_id(doc, id);
    if (!element) return;
    (void)kui_element_set_value(element, value, 0);
    kui_element_release(element);
}

/* Registers `fn` for `event` on the element with that id. Returns 0 on success. */
static inline int kuex_on(KuiDocument* doc, const char* id, const char* event,
                          KuiEventListenerFn fn, void* user_data) {
    KuiElement* element = kuex_by_id(doc, id);
    KuiStatus status;
    if (!element) {
        fprintf(stderr, "example port: no element with id '%s'\n", id);
        return 1;
    }
    status = kui_element_add_event_listener(element, event, 0, fn, user_data, NULL);
    kui_element_release(element);
    return status == KUI_OK ? 0 : 1;
}

/* Copies an element's textContent into buf (always NUL-terminated, truncated when too long). */
static inline void kuex_element_text(KuiElement* element, char* buf, size_t capacity) {
    size_t length = 0;
    if (capacity == 0) return;
    buf[0] = '\0';
    if (!element) return;
    kui_element_get_text_content(element, buf, capacity, &length);
    buf[capacity - 1] = '\0';
}

/* textContent of the element with that id; empty string when absent. */
static inline void kuex_get_text(KuiDocument* doc, const char* id, char* buf, size_t capacity) {
    KuiElement* element = kuex_by_id(doc, id);
    if (capacity) buf[0] = '\0';
    if (!element) return;
    kuex_element_text(element, buf, capacity);
    kui_element_release(element);
}

/* textContent of the event's target element (C++: e.target()->textContent()); empty when the
 * target is not an element. */
static inline void kuex_target_text(KuiEvent* event, char* buf, size_t capacity) {
    KuiElement* target = NULL;
    if (capacity) buf[0] = '\0';
    if (kui_event_get_target(event, &target) != KUI_OK || !target) return;
    kuex_element_text(target, buf, capacity);
    kui_element_release(target);
}

/* C++: doc->createElement(tag), setTextContent(text) when not empty, then style()->setProperty
 * for each pair. `styles` is a NULL-terminated list of name, value, name, value ... (may be
 * NULL). Returns a retained element or NULL; release with kui_element_release. */
static inline KuiElement* kuex_make_element(KuiDocument* doc, const char* tag, const char* text,
                                            const char* const* styles) {
    KuiElement* element = NULL;
    if (kui_document_create_element(doc, tag, 0, &element) != KUI_OK) return NULL;
    if (text && *text) kui_element_set_text_content(element, text, 0);
    for (; styles && styles[0] && styles[1]; styles += 2) {
        kui_element_set_style_property(element, styles[0], 0, styles[1], 0, 0);
    }
    return element;
}

/* parent->appendChild(child), then drops the caller's reference to child. */
static inline void kuex_append_release(KuiElement* parent, KuiElement* child) {
    if (!child) return;
    if (parent) kui_element_append_child(parent, child);
    kui_element_release(child);
}

#endif
