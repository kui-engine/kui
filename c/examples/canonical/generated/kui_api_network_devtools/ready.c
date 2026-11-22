/* Copied by scripts/generate_c_examples.py from examples/c/ports/kui_api_network_devtools.c; edit that file, not this copy. */
/* API tour: HTTP client and in-process DevTools (examples/api_tours/network_devtools). The host
 * fetches from the window's own DevTools server (a loopback HTTP server: /json/version) with the
 * sync, worker-thread and main-thread request functions, provokes transport errors (closed port,
 * malformed URL, cancel), runs raw CDP commands, injects mouse / key / IME input and reads box
 * models. No internet access is needed. One #tour-log row per check; #tour-result ends as
 * "PASS n/n" once every asynchronous answer is in. The panel buttons repeat the calls. */
#include "kui_c_example_dom.h"

#include <stdarg.h>
#include <stdlib.h>

static KuiDocument* g_doc;
static KuiWindow* g_window;

/* ---- Self-check bookkeeping: count, log one <tr> per check, publish the verdict ----------
 * check():  a documented behaviour; a failure makes the tour FAIL.
 * known():  a documented behaviour the engine currently gets wrong (reported bug). */

static int g_total;
static int g_failed;
static int g_known;
static int g_finished;
static char g_failed_names[512];

static void log_row(const char* name, const char* result, const char* cell) {
    KuiElement* log = kuex_by_id(g_doc, "tour-log");
    KuiElement* row = NULL;
    if (!log) return;
    if (kui_document_create_element(g_doc, "tr", 0, &row) == KUI_OK) {
        kui_element_set_attribute(row, "data-check", 0, name, 0);
        kui_element_set_attribute(row, "data-result", 0, result, 0);
        if (strcmp(result, "ok") != 0) kui_element_class_list_add(row, "bad", 0);
        kuex_append_release(row, kuex_make_element(g_doc, "td", name, NULL));
        kuex_append_release(row, kuex_make_element(g_doc, "td", cell, NULL));
        kuex_append_release(log, row);
    }
    kui_element_release(log);
}

static void check(const char* name, int ok, const char* fmt, ...) {
    char detail[256];
    char cell[300];
    size_t used = strlen(g_failed_names);
    va_list args;

    g_total++;
    if (ok) {
        log_row(name, "ok", "ok");
        return;
    }
    va_start(args, fmt);
    vsnprintf(detail, sizeof detail, fmt, args);
    va_end(args);
    g_failed++;
    snprintf(g_failed_names + used, sizeof g_failed_names - used, "%s%s", used ? ", " : "", name);
    snprintf(cell, sizeof cell, "FAIL: %s", detail);
    fprintf(stderr, "tour check failed: %s: %s\n", name, detail);
    log_row(name, "fail", cell);
}

static void known(const char* name, int ok, const char* bug, const char* fmt, ...) {
    char detail[256];
    char cell[400];
    va_list args;

    if (ok) {
        g_total++;
        log_row(name, "ok", "ok");
        return;
    }
    va_start(args, fmt);
    vsnprintf(detail, sizeof detail, fmt, args);
    va_end(args);
    g_known++;
    snprintf(cell, sizeof cell, "KNOWN BUG: %s (%s)", bug, detail);
    fprintf(stderr, "tour known bug: %s: %s\n", name, detail);
    log_row(name, "known", cell);
}

static void finish(void) {
    char text[640];
    char number[16];
    size_t used;
    KuiElement* result;
    if (g_finished) return;
    g_finished = 1;
    result = kuex_by_id(g_doc, "tour-result");
    if (!result) return;
    if (g_failed == 0) {
        snprintf(text, sizeof text, "PASS %d/%d", g_total, g_total);
    } else {
        snprintf(text, sizeof text, "FAIL %d/%d: %s", g_failed, g_total, g_failed_names);
    }
    used = strlen(text);
    if (g_known) snprintf(text + used, sizeof text - used, " (known bugs: %d)", g_known);
    kui_element_set_text_content(result, text, 0);
    snprintf(number, sizeof number, "%d", g_total);
    kui_element_set_attribute(result, "data-total", 0, number, 0);
    snprintf(number, sizeof number, "%d", g_failed);
    kui_element_set_attribute(result, "data-failed", 0, number, 0);
    snprintf(number, sizeof number, "%d", g_known);
    kui_element_set_attribute(result, "data-known", 0, number, 0);
    kui_element_class_list_add(result, g_failed == 0 ? "pass" : "fail", 0);
    kui_element_release(result);
}

/* Asynchronous checks still waiting for their callback; the verdict is published when the last
 * one arrives (or by the watchdog, which fails whatever is still missing). */
static int g_pending;
static int g_ready_done;

static void settle(void) {
    if (--g_pending == 0 && g_ready_done) finish();
}

/* ---- Response readers ------------------------------------------------------------------------ */

typedef struct Reply {
    int status;
    char content_type[128];
    char error[256];
    char body[600];
    size_t body_len;
} Reply;

/* Copies everything out of a response and releases it (the callback owns the handle). */
static void take_reply(KuiHttpResponse* response, Reply* reply) {
    size_t length = 0;
    memset(reply, 0, sizeof *reply);
    if (!response) {
        snprintf(reply->error, sizeof reply->error, "no response object");
        return;
    }
    kui_http_response_get_status(response, &reply->status);
    kui_http_response_get_content_type(response, reply->content_type, sizeof reply->content_type,
                                       &length);
    kui_http_response_get_error(response, reply->error, sizeof reply->error, &length);
    /* The body getter reports the full length; a buffer one byte larger holds it NUL-terminated. */
    kui_http_response_get_body(response, NULL, 0, &reply->body_len);
    kui_http_response_get_body(response, (uint8_t*)reply->body, sizeof reply->body, &length);
    reply->body[sizeof reply->body - 1] = '\0';
    kui_http_response_release(response);
}

static int is_json_version(const Reply* reply) {
    return reply->status == 200 && strstr(reply->content_type, "application/json") != NULL &&
           reply->body[0] == '{' && strstr(reply->body, "\"Browser\":\"KUI/") != NULL &&
           reply->error[0] == '\0';
}

static void show_reply(const char* prefix, const Reply* reply) {
    char text[512];
    snprintf(text, sizeof text, "%s status=%d type=%s bytes=%u%s%s", prefix, reply->status,
             reply->content_type[0] ? reply->content_type : "-", (unsigned)reply->body_len,
             reply->error[0] ? " error=" : "", reply->error);
    kuex_set_text(g_doc, "http-out", text);
    kuex_set_text(g_doc, "http-body", reply->body[0] ? reply->body : "-");
}

/* ---- HTTP against the loopback DevTools server --------------------------------------------- */

static KuiHttpClient* g_client;
static KuiHttpClient* g_cancel_client;
static char g_version_url[96];
static const char* k_closed_url = "http://127.0.0.1:1/";   /* nothing listens on port 1 */

/* Worker-thread result, handed to the main thread with kui_platform_post_to_main_thread. */
typedef struct WorkerResult {
    Reply reply;
    int self_check;
    const char* name;
} WorkerResult;

static void on_worker_result_main(void* user_data) {
    WorkerResult* result = (WorkerResult*)user_data;
    if (result->self_check) {
        if (strcmp(result->name, "http.fetch_async") == 0) {
            check(result->name, is_json_version(&result->reply), "status %d type '%s' error '%s'",
                  result->reply.status, result->reply.content_type, result->reply.error);
        } else {
            check(result->name, result->reply.status == 0 && result->reply.error[0] != '\0',
                  "status %d error '%s'", result->reply.status, result->reply.error);
        }
        settle();
    } else {
        show_reply("worker + cancel_all:", &result->reply);
    }
    free(result);
}

static void discard_worker_result(void* user_data) { free(user_data); }

/* Runs on an HTTP worker thread: read the response here, touch the DOM on the main thread. */
static void on_worker_response(KuiHttpResponse* response, void* user_data) {
    WorkerResult* result = (WorkerResult*)user_data;
    take_reply(response, &result->reply);
    if (kui_platform_post_to_main_thread(on_worker_result_main, discard_worker_result, result) !=
        KUI_OK) {
        free(result);
    }
}

static int start_worker_fetch(KuiHttpClient* client, const char* url, const char* name,
                              int self_check) {
    WorkerResult* result = (WorkerResult*)calloc(1, sizeof *result);
    if (!result) return 0;
    result->name = name;
    result->self_check = self_check;
    if (kui_http_client_fetch_async(client, url, 0, 3.0, on_worker_response, result) != KUI_OK) {
        free(result);
        return 0;
    }
    return 1;
}

/* Main-thread callbacks of the self-check. */
static void on_main_version(KuiHttpResponse* response, void* user_data) {
    Reply reply;
    (void)user_data;
    take_reply(response, &reply);
    check("http.fetch_async_on_main_thread", g_ready_done && is_json_version(&reply),
          "after ready %d, status %d type '%s' error '%s'", g_ready_done, reply.status,
          reply.content_type, reply.error);
    show_reply("GET /json/version (main thread):", &reply);
    settle();
}

static void on_main_closed(KuiHttpResponse* response, void* user_data) {
    Reply reply;
    (void)user_data;
    take_reply(response, &reply);
    check("http.response_get_error-closed-port", reply.status == 0 && reply.error[0] != '\0' &&
                                                     reply.body_len == 0,
          "status %d error '%s' bytes %u", reply.status, reply.error, (unsigned)reply.body_len);
    settle();
}

static void check_http(void) {
    KuiHttpResponse* response = NULL;
    KuiStatus status;
    Reply reply;

    status = kui_http_client_create(&g_client);
    check("http.client_create", status == KUI_OK && g_client, "status %d", status);
    status = kui_http_client_set_user_agent(g_client, "kui-api-tour/1.0", 0);
    check("http.set_user_agent", status == KUI_OK, "status %d", status);
    kui_http_client_create(&g_cancel_client);

    status = kui_http_client_fetch_sync(g_client, g_version_url, 0, 3.0, &response);
    take_reply(status == KUI_OK ? response : NULL, &reply);
    check("http.fetch_sync", status == KUI_OK && is_json_version(&reply),
          "status %d http %d type '%s' error '%s'", status, reply.status, reply.content_type,
          reply.error);
    check("http.response_get_content_type", strcmp(reply.content_type, "application/json") == 0,
          "content type '%s'", reply.content_type);

    response = NULL;
    status = kui_http_client_fetch_sync(g_client, "not a url", 0, 3.0, &response);
    take_reply(status == KUI_OK ? response : NULL, &reply);
    check("http.response_get_error-bad-url", status == KUI_OK && reply.status == 0 &&
                                                 reply.error[0] != '\0',
          "status %d http %d error '%s'", status, reply.status, reply.error);
    check("http.response_get_body-empty-on-error", reply.body_len == 0, "body of %u bytes",
          (unsigned)reply.body_len);

    if (kui_http_client_fetch_async_on_main_thread(g_client, g_version_url, 0, 3.0,
                                                   on_main_version, NULL) == KUI_OK) {
        g_pending++;
    } else {
        check("http.fetch_async_on_main_thread", 0, "request not accepted");
    }
    if (kui_http_client_fetch_async_on_main_thread(g_client, k_closed_url, 0, 3.0, on_main_closed,
                                                   NULL) == KUI_OK) {
        g_pending++;
    } else {
        check("http.response_get_error-closed-port", 0, "request not accepted");
    }
    if (start_worker_fetch(g_client, g_version_url, "http.fetch_async", 1)) {
        g_pending++;
    } else {
        check("http.fetch_async", 0, "request not accepted");
    }
    /* A request that is cancelled right away still answers once, with the error set. */
    if (start_worker_fetch(g_cancel_client, k_closed_url, "http.cancel_all", 1)) {
        g_pending++;
        status = kui_http_client_cancel_all(g_cancel_client);
        if (status != KUI_OK) check("http.cancel_all-status", 0, "status %d", status);
    } else {
        check("http.cancel_all", 0, "request not accepted");
    }
}

/* ---- DevTools: port, raw CDP commands ----------------------------------------------------- */

static uint16_t g_port;

static void devtools_port(void) {
    int started = 0;
    if (kui_window_get_devtools_port(g_window, &g_port) != KUI_OK || g_port == 0) {
        /* Not started by the runner (KUI_EXAMPLE_DEVTOOLS_PORT unset): start one, port 0 picks
         * a free one. */
        kui_window_start_devtools(g_window, 0, &started);
        kui_window_get_devtools_port(g_window, &g_port);
    }
    snprintf(g_version_url, sizeof g_version_url, "http://127.0.0.1:%u/json/version",
             (unsigned)g_port);
}

/* Runs a CDP command; the response (possibly truncated) lands in buf. */
static KuiStatus cdp(const char* json, char* buf, size_t capacity, size_t* out_len) {
    *out_len = 0;
    buf[0] = '\0';
    return kui_devtools_dispatch_command(g_doc, json, buf, capacity, out_len);
}

static void check_devtools_commands(void) {
    char response[2048];
    char small[8];
    char attr[64];
    size_t length = 0;
    KuiStatus status;
    const char* env_port = getenv("KUI_EXAMPLE_DEVTOOLS_PORT");

    check("dt.window_get_devtools_port",
          g_port != 0 && (!env_port || !*env_port || atoi(env_port) == (int)g_port),
          "port %u (KUI_EXAMPLE_DEVTOOLS_PORT=%s)", (unsigned)g_port, env_port ? env_port : "");

    status = cdp("{\"id\":1,\"method\":\"DOM.setAttribute\",\"params\":{\"selector\":\"#cdp-target\","
                 "\"name\":\"data-cdp\",\"value\":\"via-command\"}}",
                 response, sizeof response, &length);
    kuex_get_attr(g_doc, "cdp-target", "data-cdp", attr, sizeof attr);
    check("dt.dispatch_command", status == KUI_OK && strstr(response, "\"id\":1") &&
                                     !strstr(response, "\"error\"") &&
                                     strcmp(attr, "via-command") == 0,
          "status %d attr '%s' response %.120s", status, attr, response);

    status = cdp("{\"id\":2,\"method\":\"Nope.nothing\"}", response, sizeof response, &length);
    check("dt.dispatch_command-unknown-method", status == KUI_OK && strstr(response, "\"error\""),
          "status %d response %.120s", status, response);

    status = cdp("{\"id\":3,\"method\":\"Page.getLayoutMetrics\"}", small, sizeof small, &length);
    check("dt.dispatch_command-buffer-too-small",
          status == KUI_ERR_BUFFER_TOO_SMALL && length >= sizeof small,
          "status %d length %u", status, (unsigned)length);
}

/* ---- DevTools token check: off for tools that cannot pass the token, then on again ------------- */

static void check_token_auth(void) {
    KuiStatus off = kui_window_set_devtools_token_auth(g_window, 0);
    KuiStatus on = kui_window_set_devtools_token_auth(g_window, 1);
    check("dt.set_devtools_token_auth", off == KUI_OK && on == KUI_OK, "off %d, on %d", (int)off, (int)on);
}


/* ---- Input injection: listeners record what arrives -------------------------------------- */

static int g_clicks;
static int g_wheels;
static float g_move_x, g_move_y;
static char g_first_key[32];
static char g_first_code[32];
static int g_shift_seen;

static void on_target_click(KuiEvent* event, void* user_data) {
    char text[32];
    (void)event;
    (void)user_data;
    snprintf(text, sizeof text, "clicks=%d", ++g_clicks);
    kuex_set_text(g_doc, "inj-target", text);
}

static void on_wheel_box(KuiEvent* event, void* user_data) {
    char text[32];
    (void)event;
    (void)user_data;
    snprintf(text, sizeof text, "wheel=%d", ++g_wheels);
    kuex_set_text(g_doc, "wheel-box", text);
}

static void on_wheel_box_move(KuiEvent* event, void* user_data) {
    (void)user_data;
    kui_event_mouse_get_client_xy(event, &g_move_x, &g_move_y);
}

static void on_input_keydown(KuiEvent* event, void* user_data) {
    size_t length = 0;
    int shift = 0;
    (void)user_data;
    if (!g_first_key[0]) {
        kui_event_key_get_key(event, g_first_key, sizeof g_first_key, &length);
        kui_event_key_get_code(event, g_first_code, sizeof g_first_code, &length);
    }
    kui_event_key_get_modifiers(event, NULL, &shift, NULL, NULL);
    if (shift) g_shift_seen = 1;
}

static void center_of(const char* id, float* x, float* y) {
    KuiElement* element = kuex_by_id(g_doc, id);
    KuiRect rect;
    memset(&rect, 0, sizeof rect);
    if (element) {
        kui_element_get_bounding_rect(element, &rect);
        kui_element_release(element);
    }
    *x = rect.x + rect.width * 0.5f;
    *y = rect.y + rect.height * 0.5f;
}

static void focus(const char* id) {
    KuiElement* element = kuex_by_id(g_doc, id);
    if (!element) return;
    kui_element_focus(element);
    kui_element_release(element);
}

static void input_value(const char* id, char* buf, size_t capacity) {
    kuex_get_value(g_doc, id, buf, capacity);
}

static void inject_click(const char* id) {
    float x, y;
    center_of(id, &x, &y);
    kui_devtools_dispatch_mouse_event(g_doc, KUI_C_MOUSE_PRESSED, x, y, KUI_C_MOUSE_BUTTON_LEFT, 0,
                                      1, 0.0f, 0.0f);
    kui_devtools_dispatch_mouse_event(g_doc, KUI_C_MOUSE_RELEASED, x, y, KUI_C_MOUSE_BUTTON_LEFT,
                                      0, 1, 0.0f, 0.0f);
}

static void inject_wheel(void) {
    float x, y;
    center_of("wheel-box", &x, &y);
    kui_devtools_dispatch_mouse_event(g_doc, KUI_C_MOUSE_WHEEL, x, y, KUI_C_MOUSE_BUTTON_NONE, 0, 0,
                                      0.0f, 120.0f);
}

/* Key down / char / key up for each letter, as a keyboard would. */
static void inject_text(const char* letters) {
    char key[2] = {0, 0};
    char code[8];
    for (; *letters; ++letters) {
        key[0] = *letters;
        snprintf(code, sizeof code, "Key%c", *letters - 'a' + 'A');
        kui_devtools_dispatch_key_event(g_doc, KUI_C_KEY_DOWN, key, 0, code, 0, 0, NULL, 0, 0);
        kui_devtools_dispatch_key_event(g_doc, KUI_C_KEY_CHAR, key, 0, code, 0, 0, key, 0, 0);
        kui_devtools_dispatch_key_event(g_doc, KUI_C_KEY_UP, key, 0, code, 0, 0, NULL, 0, 0);
    }
}

/* Start, an intermediate composition, then the committed text ("ni" -> U+4F60). */
static void inject_ime(void) {
    kui_devtools_dispatch_ime_event(g_doc, KUI_C_IME_START, "", 0);
    kui_devtools_dispatch_ime_event(g_doc, KUI_C_IME_UPDATE, "ni", 0);
    kui_devtools_dispatch_ime_event(g_doc, KUI_C_IME_COMMIT, "\xe4\xbd\xa0", 0);
}

static void check_injection(void) {
    char value[64];
    float x, y;
    KuiStatus status;

    inject_click("inj-target");
    check("dt.dispatch_mouse_event-click", g_clicks == 1, "clicks %d", g_clicks);

    center_of("wheel-box", &x, &y);
    status = kui_devtools_dispatch_mouse_event(g_doc, KUI_C_MOUSE_MOVED, x + 3.0f, y + 2.0f,
                                               KUI_C_MOUSE_BUTTON_NONE, 0, 0, 0.0f, 0.0f);
    check("dt.dispatch_mouse_event-move",
          status == KUI_OK && g_move_x - (x + 3.0f) < 0.01f && (x + 3.0f) - g_move_x < 0.01f &&
              g_move_y - (y + 2.0f) < 0.01f && (y + 2.0f) - g_move_y < 0.01f,
          "status %d moved to (%.1f, %.1f), listener saw (%.1f, %.1f)", status, x + 3.0f, y + 2.0f,
          g_move_x, g_move_y);
    inject_wheel();
    check("dt.dispatch_mouse_event-wheel", g_wheels == 1, "wheel events %d", g_wheels);
    status = kui_devtools_dispatch_mouse_event(g_doc, (KuiCMouseEventType)9, 0.0f, 0.0f,
                                               KUI_C_MOUSE_BUTTON_NONE, 0, 0, 0.0f, 0.0f);
    check("dt.dispatch_mouse_event-invalid-type", status == KUI_ERR_INVALID_ARG, "status %d",
          status);

    focus("inj-input");
    inject_text("kui");
    input_value("inj-input", value, sizeof value);
    check("dt.dispatch_key_event", strcmp(value, "kui") == 0 && strcmp(g_first_key, "k") == 0 &&
                                       strcmp(g_first_code, "KeyK") == 0,
          "value '%s', first keydown key '%s' code '%s'", value, g_first_key, g_first_code);
    kui_devtools_dispatch_key_event(g_doc, KUI_C_KEY_DOWN, "Shift", 0, "ShiftLeft", 0, 8, NULL, 0,
                                    0);
    kui_devtools_dispatch_key_event(g_doc, KUI_C_KEY_UP, "Shift", 0, "ShiftLeft", 0, 8, NULL, 0, 0);
    check("dt.dispatch_key_event-modifiers", g_shift_seen, "keydown listener saw no Shift");

    focus("ime-input");
    inject_ime();
    input_value("ime-input", value, sizeof value);
    check("dt.dispatch_ime_event", strcmp(value, "\xe4\xbd\xa0") == 0, "value '%s'", value);
}

/* ---- Box model: four quads vs the bounding rect ------------------------------------------- */

typedef struct Quads {
    float q[32];
    float width, height;
} Quads;

static float quad_w(const Quads* b, int box) { return b->q[box * 8 + 2] - b->q[box * 8 + 0]; }
static float quad_h(const Quads* b, int box) { return b->q[box * 8 + 5] - b->q[box * 8 + 1]; }
static int close_to(float a, float b) { return a - b < 0.5f && b - a < 0.5f; }

static void check_box_model(void) {
    Quads b;
    KuiRect rect;
    KuiElement* element = kuex_by_id(g_doc, "box-target");
    KuiStatus status = kui_devtools_get_box_model(g_doc, "#box-target", b.q, &b.width, &b.height);

    memset(&rect, 0, sizeof rect);
    if (element) {
        kui_element_get_bounding_rect(element, &rect);
        kui_element_release(element);
    }
    check("dt.get_box_model", status == KUI_OK && close_to(b.width, 120.0f) &&
                                  close_to(b.height, 40.0f) && close_to(quad_w(&b, 1), 140.0f) &&
                                  close_to(quad_w(&b, 2), 148.0f) && close_to(quad_h(&b, 2), 68.0f) &&
                                  close_to(quad_w(&b, 3), 160.0f) &&
                                  close_to(b.q[0] - b.q[16], 14.0f),
          "status %d content %.1fx%.1f padding %.1f border %.1fx%.1f margin %.1f", status,
          b.width, b.height, quad_w(&b, 1), quad_w(&b, 2), quad_h(&b, 2), quad_w(&b, 3));
    check("dt.get_box_model-vs-bounding-rect",
          close_to(b.q[16], rect.x) && close_to(b.q[17], rect.y) &&
              close_to(quad_w(&b, 2), rect.width) && close_to(quad_h(&b, 2), rect.height),
          "border quad (%.1f, %.1f) vs rect (%.1f, %.1f %.1fx%.1f)", b.q[16], b.q[17], rect.x,
          rect.y, rect.width, rect.height);
    status = kui_devtools_get_box_model(g_doc, "#no-such-box", b.q, &b.width, &b.height);
    check("dt.get_box_model-missing", status == KUI_ERR_NOT_FOUND, "status %d", status);
}

/* ---- Panel buttons ----------------------------------------------------------------------------- */

static void on_ui_main_reply(KuiHttpResponse* response, void* user_data) {
    Reply reply;
    take_reply(response, &reply);
    show_reply((const char*)user_data, &reply);
}

static void on_http_main(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_http_client_fetch_async_on_main_thread(g_client, g_version_url, 0, 3.0, on_ui_main_reply,
                                               (void*)"GET (main thread):");
}

static void on_http_sync(KuiEvent* e, void* u) {
    KuiHttpResponse* response = NULL;
    Reply reply;
    (void)e;
    (void)u;
    if (kui_http_client_fetch_sync(g_client, g_version_url, 0, 3.0, &response) != KUI_OK) {
        response = NULL;
    }
    take_reply(response, &reply);
    show_reply("GET (sync):", &reply);
}

static void on_http_closed(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_http_client_fetch_async_on_main_thread(g_client, k_closed_url, 0, 3.0, on_ui_main_reply,
                                               (void*)"closed port:");
}

static void on_http_bad(KuiEvent* e, void* u) {
    KuiHttpResponse* response = NULL;
    Reply reply;
    (void)e;
    (void)u;
    if (kui_http_client_fetch_sync(g_client, "not a url", 0, 3.0, &response) != KUI_OK) {
        response = NULL;
    }
    take_reply(response, &reply);
    show_reply("bad URL:", &reply);
}

static void on_http_cancel(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    if (start_worker_fetch(g_cancel_client, k_closed_url, "button", 0)) {
        kui_http_client_cancel_all(g_cancel_client);
    }
}

static void show_cdp(const char* json) {
    char response[2048];
    size_t length = 0;
    KuiStatus status = cdp(json, response, sizeof response, &length);
    char text[400];
    snprintf(text, sizeof text, "status=%d %.300s", status, response);
    kuex_set_text(g_doc, "cdp-out", text);
}

static void on_cdp_attr(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    show_cdp("{\"id\":11,\"method\":\"DOM.setAttribute\",\"params\":{\"selector\":\"#cdp-target\","
             "\"name\":\"data-cdp\",\"value\":\"button\"}}");
}

static void on_cdp_metrics(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    show_cdp("{\"id\":12,\"method\":\"Page.getLayoutMetrics\"}");
}

static void on_cdp_unknown(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    show_cdp("{\"id\":13,\"method\":\"Nope.nothing\"}");
}

static void on_inj_click(KuiEvent* e, void* u) {
    char text[64];
    (void)e;
    (void)u;
    inject_click("inj-target");
    snprintf(text, sizeof text, "injected click -> clicks=%d", g_clicks);
    kuex_set_text(g_doc, "inj-out", text);
}

static void on_inj_type(KuiEvent* e, void* u) {
    char value[64];
    char text[96];
    (void)e;
    (void)u;
    focus("inj-input");
    inject_text("kui");
    input_value("inj-input", value, sizeof value);
    snprintf(text, sizeof text, "typed -> value=%s", value);
    kuex_set_text(g_doc, "inj-out", text);
}

static void on_inj_ime(KuiEvent* e, void* u) {
    char value[64];
    char text[96];
    (void)e;
    (void)u;
    focus("ime-input");
    inject_ime();
    input_value("ime-input", value, sizeof value);
    snprintf(text, sizeof text, "IME -> value=%s", value);
    kuex_set_text(g_doc, "inj-out", text);
}

static void on_inj_wheel(KuiEvent* e, void* u) {
    char text[64];
    (void)e;
    (void)u;
    inject_wheel();
    snprintf(text, sizeof text, "injected wheel -> wheel=%d", g_wheels);
    kuex_set_text(g_doc, "inj-out", text);
}

static void on_box(KuiEvent* e, void* u) {
    Quads b;
    KuiRect rect;
    KuiElement* element = kuex_by_id(g_doc, "box-target");
    char text[200];
    (void)e;
    (void)u;
    memset(&b, 0, sizeof b);
    memset(&rect, 0, sizeof rect);
    kui_devtools_get_box_model(g_doc, "#box-target", b.q, &b.width, &b.height);
    if (element) {
        kui_element_get_bounding_rect(element, &rect);
        kui_element_release(element);
    }
    snprintf(text, sizeof text, "content=%.0fx%.0f padding=%.0fx%.0f border=%.0fx%.0f "
             "margin=%.0fx%.0f | rect=%.0fx%.0f", b.width, b.height, quad_w(&b, 1), quad_h(&b, 1),
             quad_w(&b, 2), quad_h(&b, 2), quad_w(&b, 3), quad_h(&b, 3), rect.width, rect.height);
    kuex_set_text(g_doc, "box-out", text);
}

/* Fails whatever asynchronous check has not answered within 2.6 s, then publishes. */
static void watchdog(void* user_data) {
    (void)user_data;
    if (g_finished) return;
    if (g_pending > 0) check("http.async-answers", 0, "%d request(s) never answered", g_pending);
    finish();
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    KuiTimerId timer = 0;
    int failed = 0;
    g_doc = host->document;
    g_window = host->window;

    failed |= kuex_on(g_doc, "inj-target", "click", on_target_click, NULL);
    failed |= kuex_on(g_doc, "wheel-box", "wheel", on_wheel_box, NULL);
    failed |= kuex_on(g_doc, "wheel-box", "mousemove", on_wheel_box_move, NULL);
    failed |= kuex_on(g_doc, "inj-input", "keydown", on_input_keydown, NULL);

    devtools_port();
    check_http();
    check_devtools_commands();
    check_token_auth();
    check_injection();
    check_box_model();
    g_ready_done = 1;
    (void)known; /* no engine bug in this tour yet; kept so every tour reports alike */
    if (g_pending == 0) finish();
    kui_platform_set_timeout(watchdog, NULL, 2600, &timer);

    failed |= kuex_on(g_doc, "btn-http-main", "click", on_http_main, NULL);
    failed |= kuex_on(g_doc, "btn-http-sync", "click", on_http_sync, NULL);
    failed |= kuex_on(g_doc, "btn-http-closed", "click", on_http_closed, NULL);
    failed |= kuex_on(g_doc, "btn-http-bad", "click", on_http_bad, NULL);
    failed |= kuex_on(g_doc, "btn-http-cancel", "click", on_http_cancel, NULL);
    failed |= kuex_on(g_doc, "btn-cdp-attr", "click", on_cdp_attr, NULL);
    failed |= kuex_on(g_doc, "btn-cdp-metrics", "click", on_cdp_metrics, NULL);
    failed |= kuex_on(g_doc, "btn-cdp-unknown", "click", on_cdp_unknown, NULL);
    failed |= kuex_on(g_doc, "btn-inj-click", "click", on_inj_click, NULL);
    failed |= kuex_on(g_doc, "btn-inj-type", "click", on_inj_type, NULL);
    failed |= kuex_on(g_doc, "btn-inj-ime", "click", on_inj_ime, NULL);
    failed |= kuex_on(g_doc, "btn-inj-wheel", "click", on_inj_wheel, NULL);
    failed |= kuex_on(g_doc, "btn-box", "click", on_box, NULL);
    return failed;
}
