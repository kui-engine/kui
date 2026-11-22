#include "example_common.h"

int main(int argc, char** argv) {
    KuiDocument* document = NULL;
    size_t loaded_count = 0;
    int loaded = 0;

    if (argc < 2) {
        puts("Usage: extension_3d_demo <directory-containing-kui_ext_3d.dll>");
        return 0;
    }
    KUI_EXAMPLE_CHECK(kui_document_create(&document));
    KUI_EXAMPLE_CHECK(kui_document_init_extensions(
        document, argv[1], 0, &loaded_count));
    KUI_EXAMPLE_CHECK(kui_document_extension_is_loaded(
        document, "kui-3d-view", 0, &loaded));
    printf("extensions loaded=%zu, kui-3d-view=%s\n",
           loaded_count, loaded ? "yes" : "no");
    KUI_EXAMPLE_CHECK(kui_document_release(document));
    return loaded ? 0 : 2;
}
