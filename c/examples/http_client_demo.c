#include "example_common.h"

#include <stdlib.h>

int main(int argc, char** argv) {
    KuiHttpClient* client = NULL;
    KuiHttpResponse* response = NULL;
    int status = 0;

    KUI_EXAMPLE_CHECK(kui_http_client_create(&client));
    KUI_EXAMPLE_CHECK(kui_http_client_set_user_agent(
        client, "kui-c-full-profile-example/1.0", 0));

    if (argc < 2) {
        puts("HTTP client created. Pass a URL to perform a synchronous GET.");
        KUI_EXAMPLE_CHECK(kui_http_client_cancel_all(client));
        KUI_EXAMPLE_CHECK(kui_http_client_release(client));
        return 0;
    }

    KUI_EXAMPLE_CHECK(kui_http_client_fetch_sync(
        client, argv[1], 0, 15.0, &response));
    KUI_EXAMPLE_CHECK(kui_http_response_get_status(response, &status));
    printf("HTTP status: %d\n", status);
    KUI_EXAMPLE_CHECK(kui_http_response_release(response));
    KUI_EXAMPLE_CHECK(kui_http_client_release(client));
    return 0;
}
