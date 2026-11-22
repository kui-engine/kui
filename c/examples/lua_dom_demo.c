#include "example_common.h"

#include <string.h>

int main(void) {
    KuiDocument* document = NULL;
    KuiScriptEngine* engine = NULL;
    KuiElement* output = NULL;
    char text[128];
    char error[512];
    size_t text_len = 0;
    size_t error_len = 0;
    int success = 0;

    KUI_EXAMPLE_CHECK(kui_document_create(&document));
    KUI_EXAMPLE_CHECK(kui_document_load_html(
        document, "<html><body><div id='output'>waiting</div></body></html>", 0));
    KUI_EXAMPLE_CHECK(kui_script_engine_create(&engine));
    KUI_EXAMPLE_CHECK(kui_script_engine_bind_document(engine, document));
    KUI_EXAMPLE_CHECK(kui_script_engine_eval(
        engine,
        "document:getElementById('output').textContent = 'Lua result: ' .. tostring(6 * 7)",
        0, "lua_dom_demo", 0, &success, error, sizeof error, &error_len));
    if (!success) {
        fprintf(stderr, "Lua failed: %.*s\n", (int)error_len, error);
        return 2;
    }
    KUI_EXAMPLE_CHECK(kui_document_get_element_by_id(document, "output", 0, &output));
    KUI_EXAMPLE_CHECK(kui_element_get_text_content(
        output, text, sizeof text, &text_len));
    printf("%s\n", text);

    KUI_EXAMPLE_CHECK(kui_element_release(output));
    KUI_EXAMPLE_CHECK(kui_script_engine_release(engine));
    KUI_EXAMPLE_CHECK(kui_document_release(document));
    return strcmp(text, "Lua result: 42") == 0 ? 0 : 3;
}
