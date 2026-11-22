#include <kui_c.h>

#include <stdio.h>
#include <string.h>

typedef struct App {
    KuiElement* count; /* #count, looked up once */
    int clicks;
} App;

/* evt is borrowed: valid only during this call, never store or release it. */
static void on_click(KuiEvent* evt, void* user_data) {
    App* app = (App*)user_data;
    char text[16];
    (void)evt;
    snprintf(text, sizeof text, "%d", ++app->clicks);
    kui_element_set_text_content(app->count, text, 0); /* the DOM update */
}

int main(void) {
    KuiWindowProps props;
    KuiWindow* win = NULL;
    KuiDocument* doc = NULL;
    KuiElement* button = NULL;
    App app = {NULL, 0};
    int exit_code = 0;

    if (kui_platform_init() != KUI_OK) return 1;

    memset(&props, 0, sizeof props); /* 0 = library default for every field */
    props.title = "KUI Quick Start";
    props.width = 560; /* client area */
    props.height = 640;
    props.no_resize = 1;
    props.centered = 1;
    if (kui_window_create(&props, &win) != KUI_OK) return 1;

    /* The window owns its document; this hands back a counted reference. */
    kui_window_get_document(win, &doc);
    /* A relative path is resolved against the current directory. */
    kui_document_load_html_file(doc, "app.html", 0);
    if (kui_document_get_element_by_id(doc, "count", 0, &app.count) != KUI_OK) {
        fprintf(stderr, "app.html not found in the current directory\n");
        exit_code = 1;
    } else {
        kui_document_get_element_by_id(doc, "inc", 0, &button);
        kui_element_add_event_listener(button, "click", 0, on_click, &app, NULL);
        kui_platform_run(&exit_code); /* blocks until the window is closed */
        kui_element_release(button);
        kui_element_release(app.count);
    }

    kui_document_release(doc);
    kui_window_release(win); /* destroys the window */
    kui_platform_shutdown();
    return exit_code;
}
