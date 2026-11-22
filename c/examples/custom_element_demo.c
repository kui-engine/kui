#include "example_common.h"

#include <string.h>

static int connected_count = 0;
static int attribute_count = 0;

static void on_connected(KuiElement* element, void* user_data) {
    (void)element;
    (void)user_data;
    ++connected_count;
}

static void on_attribute(KuiElement* element,
                         const char* name, size_t name_len,
                         const char* old_value, size_t old_len,
                         const char* new_value, size_t new_len,
                         void* user_data) {
    (void)element;
    (void)old_value;
    (void)old_len;
    (void)new_value;
    (void)new_len;
    (void)user_data;
    if (name_len == 5 && memcmp(name, "value", 5) == 0) ++attribute_count;
}

int main(void) {
    KuiDocument* document = NULL;
    KuiElement* element = NULL;
    const char* observed[] = {"value"};
    KuiCCustomElementCallbacks callbacks;

    memset(&callbacks, 0, sizeof callbacks);
    callbacks.connected = on_connected;
    callbacks.attribute_changed = on_attribute;
    callbacks.observed_attributes = observed;
    callbacks.observed_attribute_count = 1;

    KUI_EXAMPLE_CHECK(kui_document_create(&document));
    KUI_EXAMPLE_CHECK(kui_document_define_custom_element(
        document, "x-counter", 0, &callbacks));
    KUI_EXAMPLE_CHECK(kui_document_load_html(
        document, "<html><body><x-counter id='counter'></x-counter></body></html>", 0));
    KUI_EXAMPLE_CHECK(kui_document_get_element_by_id(
        document, "counter", 0, &element));
    KUI_EXAMPLE_CHECK(kui_element_set_attribute(element, "value", 0, "42", 0));
    printf("connected=%d attribute_changed=%d\n",
           connected_count, attribute_count);

    KUI_EXAMPLE_CHECK(kui_element_release(element));
    KUI_EXAMPLE_CHECK(kui_document_release(document));
    return connected_count > 0 && attribute_count == 1 ? 0 : 2;
}
