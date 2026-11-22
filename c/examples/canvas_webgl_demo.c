#include "example_common.h"

int main(void) {
    KuiDocument* document = NULL;
    KuiScriptEngine* engine = NULL;
    char error[1024];
    size_t error_len = 0;
    int success = 0;
    const char* script =
        "local c2d = document:getElementById('canvas2d') "
        "local ctx = c2d:getContext('2d') "
        "ctx.fillStyle = '#2563eb' ctx:fillRect(8, 8, 80, 48) "
        "local cgl = document:getElementById('webgl') "
        "local gl = cgl:getContext('webgl') "
        "canvas_webgl_ready = (ctx ~= nil and gl ~= nil)";

    KUI_EXAMPLE_CHECK(kui_document_create(&document));
    KUI_EXAMPLE_CHECK(kui_document_load_html(
        document,
        "<html><body><canvas id='canvas2d' width='160' height='100'></canvas>"
        "<canvas id='webgl' width='160' height='100'></canvas></body></html>", 0));
    KUI_EXAMPLE_CHECK(kui_script_engine_create(&engine));
    KUI_EXAMPLE_CHECK(kui_script_engine_bind_document(engine, document));
    KUI_EXAMPLE_CHECK(kui_script_engine_eval(
        engine, script, 0, "canvas_webgl_demo", 0,
        &success, error, sizeof error, &error_len));
    if (!success) {
        fprintf(stderr, "Canvas/WebGL Lua failed: %.*s\n", (int)error_len, error);
    } else {
        puts("Canvas2D and WebGL contexts created through the C ABI Lua bridge.");
    }
    KUI_EXAMPLE_CHECK(kui_script_engine_release(engine));
    KUI_EXAMPLE_CHECK(kui_document_release(document));
    return success ? 0 : 2;
}
