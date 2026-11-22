#include "example_common.h"

#include <string.h>

int main(int argc, char** argv) {
    KuiDocument* document = NULL;
    KuiCPDFOptions options;
    int written = 0;
    const char* path = argc > 1 ? argv[1] : "kui-c-example.pdf";

    memset(&options, 0, sizeof options);
    options.title = "KUI C ABI PDF example";
    options.author = "KUI";
    options.page_width_pt = 612.0f;
    options.page_height_pt = 792.0f;
    options.generate_outline = 1;

    KUI_EXAMPLE_CHECK(kui_document_create(&document));
    KUI_EXAMPLE_CHECK(kui_document_load_html(
        document,
        "<html><body style='font-family:sans-serif;padding:40px'>"
        "<h1>PDF from the KUI C ABI</h1><p>Full Profile export example.</p>"
        "</body></html>", 0));
    KUI_EXAMPLE_CHECK(kui_document_print_to_pdf(
        document, path, 0, &options, &written));
    printf("PDF %s: %s\n", path, written ? "written" : "not written");
    KUI_EXAMPLE_CHECK(kui_document_release(document));
    return written ? 0 : 2;
}
