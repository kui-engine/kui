/* Full Profile regression tests. This file is deliberately C11, not C++. */
#include <kui_c.h>

#include <assert.h> /* static_assert is a macro from here in C11 (MSVC accepts it without) */
#include <stdio.h>
#include <string.h>

static int passed = 0;
static int failed = 0;
static int connected = 0;
static int attribute_changed = 0;

#define CHECK(condition, message)                                             \
    do {                                                                       \
        if (condition) ++passed;                                               \
        else { ++failed; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, message); } \
    } while (0)

#define CHECK_OK(expression, message) CHECK((expression) == KUI_OK, message)

static_assert(KUI_C_EVENT_DRAG == 7, "event type ABI changed");
static_assert(KUI_C_EXT_BOOL == 4, "extension value ABI changed");
static_assert(sizeof(KuiCPositioningOptions) == 16, "positioning options ABI changed");
static_assert(sizeof(KuiCExtensionPerfStats) == 64, "extension stats ABI changed");
#if UINTPTR_MAX == UINT64_MAX
static_assert(sizeof(KuiCExtensionValue) == 48, "extension value x64 ABI changed");
static_assert(sizeof(KuiCPDFOptions) == 64, "PDF options x64 ABI changed");
static_assert(sizeof(KuiCCustomElementCallbacks) == 48,
              "custom element callbacks x64 ABI changed");
static_assert(sizeof(KuiCScriptValue) == 56, "script value x64 ABI changed");
#endif

static void on_connected(KuiElement* element, void* user_data) {
    (void)element;
    (void)user_data;
    ++connected;
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
    if (name_len == 7 && memcmp(name, "watched", 7) == 0)
        ++attribute_changed;
}

static void test_full_profile_arguments(void) {
    int value = 0;
    size_t count = 0;
    CHECK(kui_script_engine_create(NULL) == KUI_ERR_INVALID_ARG,
          "script create validates output");
    CHECK(kui_http_client_create(NULL) == KUI_ERR_INVALID_ARG,
          "HTTP create validates output");
    CHECK(kui_document_init_extensions(NULL, ".", 0, &count) == KUI_ERR_INVALID_ARG,
          "extension init validates document");
    CHECK(kui_document_custom_element_is_defined(NULL, "x-test", 0, &value)
              == KUI_ERR_INVALID_ARG,
          "custom registry validates document");
    CHECK(kui_document_print_to_pdf(NULL, "x.pdf", 0, NULL, &value)
              == KUI_ERR_INVALID_ARG,
          "PDF validates document");
}

/* Section 14: single-extension lifecycle. The hello extension (tag "kui-hello")
 * sits next to the test executable in the SDK build, under the platform's
 * shared-library name. */
#if defined(_WIN32)
#define HELLO_EXTENSION "./kui_ext_hello.dll"
#elif defined(__APPLE__)
#define HELLO_EXTENSION "./libkui_ext_hello.dylib"
#else
#define HELLO_EXTENSION "./libkui_ext_hello.so"
#endif
static void test_extension_lifecycle(void) {
    KuiDocument* document = NULL;
    CHECK_OK(kui_document_create(&document), "document for extensions");
    if (!document) return;
    size_t count = 0;
    int value = -1;
    CHECK(kui_document_load_extension(document, HELLO_EXTENSION, 0, &value) == KUI_ERR_NOT_SUPPORTED,
          "needs the extension manager first");
    CHECK_OK(kui_document_init_extensions(document, ".", 0, &count), "init extensions from the exe dir");
    int loaded = 0;
    kui_document_extension_is_loaded(document, "kui-hello", 0, &loaded);
    if (loaded) {
        CHECK_OK(kui_document_unload_extension(document, "kui-hello", 0), "unload hello");
        kui_document_extension_is_loaded(document, "kui-hello", 0, &loaded);
        CHECK(loaded == 0, "hello unloaded");
        CHECK_OK(kui_document_load_extension(document, HELLO_EXTENSION, 0, &value), "load hello");
        kui_document_extension_is_loaded(document, "kui-hello", 0, &loaded);
        CHECK(value == 1 && loaded == 1, "hello loaded again");
        CHECK_OK(kui_document_reload_extension(document, HELLO_EXTENSION, 0, &value), "reload hello");
        kui_document_extension_is_loaded(document, "kui-hello", 0, &loaded);
        CHECK(value == 1 && loaded == 1, "hello reloaded");
    } else {
        printf("note: " HELLO_EXTENSION " is not next to the test; load / unload / reload paths skipped\n");
    }
    CHECK(kui_document_unload_extension(document, "kui-no-such", 0) == KUI_ERR_NOT_FOUND, "unload unknown tag");
    value = -1;
    CHECK_OK(kui_document_load_extension(document, "./no-such-extension.dll", 0, &value), "load missing DLL");
    CHECK(value == 0, "missing DLL does not load");
    int watching = 0;
    CHECK_OK(kui_document_watch_extensions(document, ".", 0), "watch directory");
    CHECK_OK(kui_document_get_extension_state(document, &count, &watching), "extension state");
#if defined(_WIN32)
    CHECK(watching == 1, "watching");
#else
    /* Tripwire for T-DEBT-POSIX-EXTENSION-HOT-RELOAD-NOT-IMPLEMENTED: the Linux / macOS core has no
     * directory watcher yet and reports "not watching". When it does, this fails; switch back to the
     * Windows assertion in the same change. */
    CHECK(watching == 0, "watching: POSIX core has no directory watcher yet");
#endif
    CHECK_OK(kui_document_watch_extensions(document, NULL, 0), "stop watching");
    CHECK_OK(kui_document_unload_all_extensions(document), "unload all");
    kui_document_get_extension_state(document, &count, &watching);
    CHECK(watching == 0 && count == 0, "stopped, nothing loaded");
    kui_document_release(document);
}

static void test_lua(void) {
    KuiDocument* document = NULL;
    KuiScriptEngine* engine = NULL;
    char error[512];
    size_t error_len = 0;
    int success = 0;

    CHECK_OK(kui_document_create(&document), "document create for Lua");
    CHECK_OK(kui_script_engine_create(&engine), "Lua engine is in Full Profile");
    if (!document || !engine) goto cleanup;
    CHECK_OK(kui_script_engine_bind_document(engine, document), "bind Lua document");
    CHECK_OK(kui_script_engine_eval(engine, "full_profile_value = 40 + 2", 0,
                                    "c-full-test", 0, &success,
                                    error, sizeof error, &error_len),
             "valid Lua eval status");
    CHECK(success == 1 && error_len == 0, "valid Lua eval succeeds");
    CHECK_OK(kui_script_engine_eval(engine, "this is not valid lua !!!", 0,
                                    "c-full-test-invalid", 0, &success,
                                    error, sizeof error, &error_len),
             "invalid Lua reports through ScriptResult");
    CHECK(success == 0 && error_len > 0, "invalid Lua has diagnostic");
    CHECK_OK(kui_script_engine_unbind_document(engine), "unbind Lua document");

cleanup:
    if (engine) kui_script_engine_release(engine);
    if (document) kui_document_release(document);
}

static void test_custom_elements_and_specialized_elements(void) {
    KuiDocument* document = NULL;
    KuiElement* element = NULL;
    KuiCCustomElementCallbacks callbacks;
    const char* observed[] = {"watched"};
    int defined = 0;
    int state = -1;

    memset(&callbacks, 0, sizeof callbacks);
    callbacks.connected = on_connected;
    callbacks.attribute_changed = on_attribute;
    callbacks.observed_attributes = observed;
    callbacks.observed_attribute_count = 1;

    CHECK_OK(kui_document_create(&document), "custom document create");
    if (!document) return;
    CHECK_OK(kui_document_define_custom_element(document, "x-c-full", 0, &callbacks),
             "define custom element");
    CHECK_OK(kui_document_custom_element_is_defined(document, "x-c-full", 0, &defined),
             "query custom element");
    CHECK(defined == 1, "custom element is registered");
    CHECK_OK(kui_document_load_html(document,
        "<html><body><x-c-full id='custom'></x-c-full><audio id='media'></audio>"
        "<lottie-player id='lottie'></lottie-player></body></html>", 0),
        "load specialized elements");
    CHECK(connected > 0, "custom connected callback crossed C ABI");
    CHECK_OK(kui_document_get_element_by_id(document, "custom", 0, &element),
             "lookup custom element");
    if (element) {
        CHECK_OK(kui_element_set_attribute(element, "ignored", 0, "x", 0),
                 "set unobserved attribute");
        CHECK(attribute_changed == 0, "unobserved attribute is filtered");
        CHECK_OK(kui_element_set_attribute(element, "watched", 0, "yes", 0),
                 "set observed attribute");
        CHECK(attribute_changed == 1, "observed attribute callback crossed C ABI");
        kui_element_release(element);
        element = NULL;
    }
    CHECK_OK(kui_document_get_element_by_id(document, "media", 0, &element),
             "lookup media element");
    if (element) {
        CHECK_OK(kui_media_get_ready_state(element, &state), "media readyState");
        CHECK(state >= 0 && state <= 4, "media readyState range");
        CHECK_OK(kui_media_get_network_state(element, &state), "media networkState");
        CHECK(state >= 0 && state <= 3, "media networkState range");
        int loop = -1, autoplay = -1, ended = -1;
        CHECK_OK(kui_media_set_loop(element, 1), "media set loop");
        CHECK_OK(kui_media_set_autoplay(element, 1), "media set autoplay");
        CHECK_OK(kui_media_get_playback_flags(element, &loop, &autoplay, &ended), "media flags");
        CHECK(loop == 1 && autoplay == 1 && ended == 0, "media flags read back");
        CHECK_OK(kui_media_load(element), "media load without src");
        CHECK_OK(kui_media_get_network_state(element, &state), "networkState after load");
        CHECK(state == 3, "load without src reports NETWORK_NO_SOURCE");
        kui_element_release(element);
        element = NULL;
    }
    CHECK_OK(kui_document_get_element_by_id(document, "lottie", 0, &element),
             "lookup Lottie element");
    if (element) {
        CHECK_OK(kui_lottie_get_complete(element, &state), "Lottie complete query");
        KuiCLottieState lottie;
        CHECK_OK(kui_lottie_set_speed(element, 2.5f), "Lottie speed");
        CHECK_OK(kui_lottie_set_loop(element, 1), "Lottie loop");
        CHECK_OK(kui_lottie_seek(element, 0.25), "Lottie seek while paused");
        CHECK_OK(kui_lottie_get_state(element, &lottie), "Lottie state");
        CHECK(lottie.playing == 0 && lottie.looping == 1 && lottie.speed == 2.5f &&
              lottie.position == 0.25 && lottie.load_state == KUI_C_LOTTIE_IDLE, "Lottie state read back");
        CHECK(kui_lottie_seek(element, 1.5) == KUI_ERR_INVALID_ARG, "seek validated");
        CHECK(kui_lottie_set_speed(element, -1.0f) == KUI_ERR_INVALID_ARG, "speed validated");
        CHECK_OK(kui_lottie_stop(element), "Lottie stop");
        kui_lottie_get_state(element, &lottie);
        CHECK(lottie.position == 0.0, "stop rewinds");
        CHECK_OK(kui_lottie_set_loop(element, 0), "Lottie loop off");
        kui_lottie_get_state(element, &lottie);
        CHECK(lottie.looping == 0, "loop off read back");
        kui_element_release(element);
    }
    kui_document_release(document);
}

static void test_http_and_platform(void) {
    KuiHttpClient* client = NULL;
    char buffer[128];
    size_t length = 0;
    CHECK_OK(kui_http_client_create(&client), "HTTP client create");
    if (client) {
        CHECK_OK(kui_http_client_set_user_agent(client, "kui-c-full-test", 0),
                 "HTTP user agent");
        CHECK_OK(kui_http_client_cancel_all(client), "HTTP cancelAll");
        CHECK_OK(kui_http_client_release(client), "HTTP client release");
    }
    CHECK_OK(kui_platform_get_os_name(buffer, sizeof buffer, &length), "OS name");
    CHECK(length > 0, "OS name is non-empty");
}

static void test_standalone_pdf(void) {
    const char* path = "kui-c-full-test.pdf";
    KuiDocument* document = NULL;
    KuiCPDFOptions options;
    FILE* stream = NULL;
    char magic[6] = {0};
    int written = 0;

    remove(path);
    memset(&options, 0, sizeof options);
    options.title = "KUI C ABI standalone PDF";
    options.page_width_pt = 612.0f;
    options.page_height_pt = 792.0f;
    options.generate_outline = 1;
    CHECK_OK(kui_document_create(&document), "standalone PDF document create");
    if (!document) return;
    CHECK_OK(kui_document_load_html(document,
        "<html><body><h1>C ABI PDF</h1><p>Headless export.</p></body></html>", 0),
        "standalone PDF load HTML");
    CHECK_OK(kui_document_print_to_pdf(document, path, 0, &options, &written),
             "standalone document PDF export");
    CHECK(written == 1, "standalone PDF reports written");
    stream = fopen(path, "rb");
    CHECK(stream != NULL, "standalone PDF file exists");
    if (stream) {
        fread(magic, 1, 5, stream);
        fclose(stream);
        CHECK(memcmp(magic, "%PDF-", 5) == 0, "standalone PDF has PDF magic");
    }
    remove(path);
    kui_document_release(document);
}

/* -- Section 16: script engine control ------------------------------------ */
static int released_add = 0, released_apply = 0, released_made = 0, released_print = 0;
static int printed = 0;
static char printed_text[64];
static KuiScriptFunction* kept_function = NULL;
static KuiScriptEngine* self_releasing_engine = NULL;

static void count_release(void* user_data) { ++*(int*)user_data; }

static KuiStatus c_add(const KuiCScriptValue* args, size_t count, KuiCScriptValue* out,
                       void* user_data) {
    static char joined[64];
    (void)user_data;
    if (count == 2 && args[0].type == KUI_C_SCRIPT_INT && args[1].type == KUI_C_SCRIPT_INT) {
        out->type = KUI_C_SCRIPT_INT;
        out->int_value = args[0].int_value + args[1].int_value;
        return KUI_OK;
    }
    if (count == 2 && args[0].type == KUI_C_SCRIPT_STRING && args[1].type == KUI_C_SCRIPT_STRING) {
        snprintf(joined, sizeof joined, "%.*s%.*s", (int)args[0].string_len, args[0].string_value,
                 (int)args[1].string_len, args[1].string_value);
        out->type = KUI_C_SCRIPT_STRING;
        out->string_value = joined;
        out->string_len = strlen(joined);
        return KUI_OK;
    }
    out->type = KUI_C_SCRIPT_STRING;
    out->string_value = "c_add wants two integers or two strings";
    return KUI_ERR_INVALID_ARG;
}

/* c_apply(fn, x): calls the Lua function from C and keeps a copy of it. */
static KuiStatus c_apply(const KuiCScriptValue* args, size_t count, KuiCScriptValue* out,
                         void* user_data) {
    int ok = 0;
    KuiCScriptValue result;
    (void)user_data;
    if (count != 2 || args[0].type != KUI_C_SCRIPT_FUNCTION || !args[0].function_value)
        return KUI_ERR_INVALID_ARG;
    if (kui_script_function_call(args[0].function_value, &args[1], 1, &ok, &result) != KUI_OK || !ok)
        return KUI_ERR_INTERNAL;
    if (!kept_function) kui_script_function_retain(args[0].function_value, &kept_function);
    *out = result; /* an integer: nothing borrowed */
    return KUI_OK;
}

static KuiStatus c_made(const KuiCScriptValue* args, size_t count, KuiCScriptValue* out,
                        void* user_data) {
    (void)user_data;
    out->type = KUI_C_SCRIPT_INT;
    out->int_value = count == 1 && args[0].type == KUI_C_SCRIPT_INT ? args[0].int_value * 100 : -1;
    return KUI_OK;
}

static KuiStatus c_release_self(const KuiCScriptValue* args, size_t count, KuiCScriptValue* out,
                                void* user_data) {
    (void)args; (void)count; (void)out; (void)user_data;
    return kui_script_engine_release(self_releasing_engine);
}

static void on_print(const char* message, size_t len, void* user_data) {
    (void)user_data;
    ++printed;
    snprintf(printed_text, sizeof printed_text, "%.*s", (int)len, message);
}

static int error_contains(KuiScriptEngine* engine, const char* needle) {
    char error[512];
    size_t len = 0;
    KuiStatus status = kui_script_engine_get_last_error(engine, error, sizeof error, &len);
    return (status == KUI_OK || status == KUI_ERR_BUFFER_TOO_SMALL) && strstr(error, needle) != NULL;
}

static void test_lua_control(void) {
    KuiScriptEngine* engine = NULL;
    KuiScriptFunction* made = NULL;
    KuiCScriptValue value;
    KuiCScriptValue args[2];
    int success = 0;
    size_t memory = 0, memory_limit = 0;
    uint64_t instruction_limit = 0;

    CHECK_OK(kui_platform_init(), "platform init for the script engine tests");
    CHECK_OK(kui_script_engine_create(&engine), "engine for section 16");
    if (!engine) return;

    /* eval with a value, and the last error */
    CHECK_OK(kui_script_engine_eval_value(engine, "return 40 + 2", 0, "v", 0, &success, &value),
             "eval_value");
    CHECK(success && value.type == KUI_C_SCRIPT_INT && value.int_value == 42, "eval_value returns 42");
    CHECK_OK(kui_script_engine_eval_value(engine, "return 'hel' .. 'lo'", 0, NULL, 0, &success, &value),
             "eval_value string");
    CHECK(success && value.type == KUI_C_SCRIPT_STRING && value.string_len == 5 &&
          strcmp(value.string_value, "hello") == 0, "eval_value returns a NUL-terminated string");
    CHECK_OK(kui_script_engine_eval_value(engine, "error('boom')", 0, NULL, 0, &success, &value),
             "eval_value failure");
    CHECK(!success && error_contains(engine, "boom"), "failed run leaves its error");
    CHECK_OK(kui_script_engine_eval_value(engine, "x = 1", 0, NULL, 0, &success, &value), "eval ok");
    CHECK(success && value.type == KUI_C_SCRIPT_NIL, "nil result");
    CHECK_OK(kui_script_engine_get_last_error(engine, NULL, 0, &memory), "last error length");
    CHECK(memory == 0, "a successful run clears the last error");

    /* C function called from Lua, with arguments, results and errors */
    CHECK_OK(kui_script_engine_register_function(engine, "c_add", 0, c_add, &released_add,
                                                 count_release), "register c_add");
    CHECK_OK(kui_script_engine_eval_value(engine, "return c_add(2, 3)", 0, NULL, 0, &success, &value),
             "call c_add");
    CHECK(success && value.type == KUI_C_SCRIPT_INT && value.int_value == 5, "c_add(2, 3) == 5");
    CHECK_OK(kui_script_engine_eval_value(engine, "return c_add('ab', 'cd')", 0, NULL, 0, &success,
                                          &value), "call c_add with strings");
    CHECK(success && value.type == KUI_C_SCRIPT_STRING && strcmp(value.string_value, "abcd") == 0,
          "string arguments and result");
    CHECK_OK(kui_script_engine_eval_value(engine, "return c_add(true)", 0, NULL, 0, &success, &value),
             "c_add error");
    CHECK(!success && error_contains(engine, "two integers"), "C error status becomes a Lua error");
    CHECK_OK(kui_script_engine_eval_value(engine, "local ok, e = pcall(c_add, 1) return ok", 0, NULL, 0,
                                          &success, &value), "pcall over a C error");
    CHECK(success && value.type == KUI_C_SCRIPT_BOOL && value.bool_value == 0, "Lua pcall catches it");

    /* globals */
    memset(&value, 0, sizeof value);
    value.type = KUI_C_SCRIPT_FLOAT;
    value.float_value = 2.5;
    CHECK_OK(kui_script_engine_set_global(engine, "g_float", 0, &value), "set float global");
    value.type = KUI_C_SCRIPT_STRING;
    value.string_value = "text";
    value.string_len = 0;
    CHECK_OK(kui_script_engine_set_global(engine, "g_text", 0, &value), "set string global");
    value.type = KUI_C_SCRIPT_USERDATA;
    value.userdata = &passed;
    CHECK_OK(kui_script_engine_set_global(engine, "g_ud", 0, &value), "set userdata global");
    value.type = KUI_C_SCRIPT_BOOL;
    value.bool_value = 1;
    CHECK_OK(kui_script_engine_set_global(engine, "g_bool", 0, &value), "set bool global");
    CHECK_OK(kui_script_engine_eval_value(engine,
             "return string.format('%s %s %s %s', g_float * 2, g_text, type(g_ud), tostring(g_bool))",
             0, NULL, 0, &success, &value), "read globals from Lua");
    CHECK(success && value.type == KUI_C_SCRIPT_STRING && strcmp(value.string_value, "5.0 text userdata true") == 0,
          "globals arrive in Lua");
    CHECK_OK(kui_script_engine_get_global(engine, "g_ud", 0, &value), "get userdata global");
    CHECK(value.type == KUI_C_SCRIPT_USERDATA && value.userdata == &passed, "userdata round trip");
    CHECK_OK(kui_script_engine_get_global(engine, "missing_global", 0, &value), "get missing global");
    CHECK(value.type == KUI_C_SCRIPT_NIL, "missing global is nil");
    value.type = KUI_C_SCRIPT_FUNCTION;
    value.function_value = NULL;
    CHECK(kui_script_engine_set_global(engine, "bad", 0, &value) == KUI_ERR_INVALID_ARG,
          "function value without a handle is rejected");

    /* Lua functions called from C */
    CHECK_OK(kui_script_engine_eval_value(engine, "function mul(a, b) return a * b end", 0, NULL, 0,
                                          &success, NULL), "define mul");
    args[0].type = KUI_C_SCRIPT_INT; args[0].int_value = 6;
    args[1].type = KUI_C_SCRIPT_INT; args[1].int_value = 7;
    CHECK_OK(kui_script_engine_call_function(engine, "mul", 0, args, 2, &success, &value), "call mul");
    CHECK(success && value.type == KUI_C_SCRIPT_INT && value.int_value == 42, "mul(6, 7) == 42");
    CHECK_OK(kui_script_engine_call_function(engine, "no_such_fn", 0, NULL, 0, &success, &value),
             "call a missing function");
    CHECK(!success && error_contains(engine, "no_such_fn"), "missing function is reported");
    CHECK_OK(kui_script_engine_get_global(engine, "mul", 0, &value), "get a Lua function");
    CHECK(value.type == KUI_C_SCRIPT_FUNCTION && value.function_value, "Lua function as a handle");
    if (value.function_value) {
        KuiScriptFunction* borrowed = value.function_value;
        CHECK(kui_script_function_release(borrowed) == KUI_ERR_INVALID_ARG, "borrowed handles are not released");
        CHECK_OK(kui_script_function_call(borrowed, args, 2, &success, &value), "call the handle");
        CHECK(success && value.type == KUI_C_SCRIPT_INT && value.int_value == 42, "handle call result");
    }

    /* a Lua function passed to C, called there and kept */
    CHECK_OK(kui_script_engine_register_function(engine, "c_apply", 0, c_apply, &released_apply,
                                                 count_release), "register c_apply");
    CHECK_OK(kui_script_engine_eval_value(engine, "return c_apply(function(x) return x * 10 end, 4)", 0,
                                          NULL, 0, &success, &value), "Lua passes a function to C");
    CHECK(success && value.type == KUI_C_SCRIPT_INT && value.int_value == 40, "C called the Lua function");
    CHECK(kept_function != NULL, "retain keeps a borrowed function");
    if (kept_function) {
        args[0].int_value = 9;
        CHECK_OK(kui_script_function_call(kept_function, args, 1, &success, &value), "call the kept function");
        CHECK(success && value.int_value == 90, "kept function still works after the call");
    }

    /* an anonymous C function handed to Lua */
    CHECK_OK(kui_script_function_create(engine, c_made, &released_made, count_release, &made),
             "create a C function");
    memset(&value, 0, sizeof value);
    value.type = KUI_C_SCRIPT_FUNCTION;
    value.function_value = made;
    CHECK_OK(kui_script_engine_set_global(engine, "c_made", 0, &value), "store it as a global");
    CHECK_OK(kui_script_function_release(made), "release the owned handle");
    CHECK(released_made == 0, "Lua still references it");
    CHECK_OK(kui_script_engine_eval_value(engine, "return c_made(3)", 0, NULL, 0, &success, &value),
             "call it from Lua");
    CHECK(success && value.int_value == 300, "C function created without a name");

    /* print */
    CHECK_OK(kui_script_engine_set_print_handler(engine, on_print, &released_print, count_release),
             "print handler");
    CHECK_OK(kui_script_engine_eval_value(engine, "print('hi', 1, true)", 0, NULL, 0, &success, NULL),
             "print from Lua");
    CHECK(printed == 1 && strcmp(printed_text, "hi\t1\ttrue") == 0, "print goes to the handler");
    CHECK_OK(kui_script_engine_set_print_handler(engine, NULL, NULL, NULL), "restore print");
    CHECK(released_print == 1, "replaced print handler is released once");
    kui_script_engine_eval_value(engine, "print('again')", 0, NULL, 0, &success, NULL);
    CHECK(printed == 1, "handler no longer called");

    /* limits */
    CHECK_OK(kui_script_engine_get_resource_usage(engine, &memory, &memory_limit, &instruction_limit),
             "resource usage");
    CHECK(memory > 0 && memory_limit == 256u * 1024 * 1024 && instruction_limit == 100000000u,
          "default limits");
    CHECK_OK(kui_script_engine_set_instruction_limit(engine, 100000), "instruction limit");
    CHECK_OK(kui_script_engine_eval_value(engine, "while true do end", 0, NULL, 0, &success, &value),
             "endless loop");
    CHECK(!success && error_contains(engine, "instruction limit"), "endless loop stopped");
    CHECK_OK(kui_script_engine_set_memory_limit(engine, memory + 1024 * 1024), "memory limit");
    CHECK_OK(kui_script_engine_eval_value(engine, "local s = string.rep('x', 8 * 1024 * 1024)", 0, NULL,
                                          0, &success, &value), "large allocation");
    CHECK(!success && error_contains(engine, "memory"), "allocation over the limit fails");
    CHECK_OK(kui_script_engine_set_memory_limit(engine, 0), "lift memory limit");
    CHECK_OK(kui_script_engine_get_resource_usage(engine, NULL, &memory_limit, &instruction_limit),
             "resource usage after changes");
    CHECK(memory_limit == 0 && instruction_limit == 100000, "limits read back");

    /* release rules */
    CHECK(released_add == 0 && released_apply == 0, "registered functions live with the engine");
    CHECK_OK(kui_script_engine_release(engine), "release engine");
    CHECK(released_add == 1 && released_apply == 1 && released_made == 1,
          "every C function released once with the engine");
    if (kept_function) {
        CHECK(kui_script_function_call(kept_function, args, 1, &success, &value) == KUI_ERR_INVALID_ARG,
              "a kept handle reports its engine is gone");
        CHECK_OK(kui_script_function_release(kept_function), "release the kept handle afterwards");
        kept_function = NULL;
    }

    /* an engine released inside its own callback is freed after the callback */
    CHECK_OK(kui_script_engine_create(&self_releasing_engine), "second engine");
    if (self_releasing_engine) {
        CHECK_OK(kui_script_engine_register_function(self_releasing_engine, "bye", 0, c_release_self,
                                                     NULL, NULL), "register bye");
        CHECK_OK(kui_script_engine_eval_value(self_releasing_engine, "bye() return 7", 0, NULL, 0,
                                              &success, &value), "release inside a callback");
        CHECK(success && value.int_value == 7, "the script finished after the release");
        for (int i = 0; i < 5; ++i) kui_platform_wait_events(0.01);
    }
    kui_platform_shutdown();
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("KUI C ABI Full Profile tests\n");
    test_full_profile_arguments();
    test_extension_lifecycle();
    test_lua();
    test_custom_elements_and_specialized_elements();
    test_http_and_platform();
    test_standalone_pdf();
    test_lua_control();
    printf("%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
