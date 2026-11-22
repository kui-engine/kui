#include "example_common.h"

int main(void) {
    KuiDocument* document = NULL;
    KuiElement* media = NULL;
    KuiElement* lottie = NULL;
    int state = 0;
    int available = 0;

    KUI_EXAMPLE_CHECK(kui_document_create(&document));
    KUI_EXAMPLE_CHECK(kui_document_load_html(
        document,
        "<html><body><audio id='media'></audio>"
        "<lottie-player id='lottie'></lottie-player></body></html>", 0));
    KUI_EXAMPLE_CHECK(kui_document_get_element_by_id(
        document, "media", 0, &media));
    KUI_EXAMPLE_CHECK(kui_media_set_controls(media, 1));
    KUI_EXAMPLE_CHECK(kui_media_set_volume(media, 0.5f));
    KUI_EXAMPLE_CHECK(kui_media_get_ready_state(media, &state));
    KUI_EXAMPLE_CHECK(kui_media_is_available(media, &available));
    printf("media readyState=%d backend_available=%d\n", state, available);

    KUI_EXAMPLE_CHECK(kui_document_get_element_by_id(
        document, "lottie", 0, &lottie));
    KUI_EXAMPLE_CHECK(kui_lottie_get_complete(lottie, &state));
    printf("lottie complete=%d (set a JSON source before play)\n", state);

    KUI_EXAMPLE_CHECK(kui_element_release(lottie));
    KUI_EXAMPLE_CHECK(kui_element_release(media));
    KUI_EXAMPLE_CHECK(kui_document_release(document));
    return 0;
}
