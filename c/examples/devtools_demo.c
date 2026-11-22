#include "example_common.h"

#include <string.h>

int main(void) {
    KuiWindowProps props;
    KuiWindow* window = NULL;
    char url[256];
    size_t url_len = 0;
    int started = 0;

    KUI_EXAMPLE_CHECK(kui_platform_init());
    memset(&props, 0, sizeof props);
    props.title = "KUI C ABI DevTools example";
    props.width = 640;
    props.height = 420;
    props.centered = 1;
    KUI_EXAMPLE_CHECK(kui_window_create(&props, &window));
    KUI_EXAMPLE_CHECK(kui_window_start_devtools(window, 0, &started));
    KUI_EXAMPLE_CHECK(kui_window_get_devtools_url(
        window, url, sizeof url, &url_len));
    printf("DevTools started=%d URL=%s\n", started, url);
    KUI_EXAMPLE_CHECK(kui_window_release(window));
    KUI_EXAMPLE_CHECK(kui_platform_shutdown());
    return started ? 0 : 2;
}
