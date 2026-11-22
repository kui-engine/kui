/* API tour: script engine control and a host-driven WebGL page (examples/api_tours/script_webgl).
 * On start the host exchanges every kind of script value with the page's Lua, calls Lua and lets
 * Lua call the host, exercises limits, print, eval_file and document re-binding, then watches the
 * WebGL scene advance, pause and resume. One row per check goes into #tour-log; #tour-result ends
 * as "PASS n/n". The panel buttons repeat the calls by hand. */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS /* getenv / fopen for the temporary Lua file */
#endif
#include "kui_c_example_dom.h"

#include <stdarg.h>
#include <stdlib.h>

static KuiDocument* g_doc;
static KuiScriptEngine* g_script;

/* ---- Self-check bookkeeping: count, log one <tr> per check, publish the verdict ---------- */

static int g_total;
static int g_failed;
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

static void finish(void) {
    char text[640];
    char number[16];
    KuiElement* result = kuex_by_id(g_doc, "tour-result");
    if (!result) return;
    if (g_failed == 0) {
        snprintf(text, sizeof text, "PASS %d/%d", g_total, g_total);
    } else {
        snprintf(text, sizeof text, "FAIL %d/%d: %s", g_failed, g_total, g_failed_names);
    }
    kui_element_set_text_content(result, text, 0);
    snprintf(number, sizeof number, "%d", g_total);
    kui_element_set_attribute(result, "data-total", 0, number, 0);
    snprintf(number, sizeof number, "%d", g_failed);
    kui_element_set_attribute(result, "data-failed", 0, number, 0);
    kui_element_set_attribute(result, "data-known", 0, "0", 0);
    kui_element_class_list_add(result, g_failed == 0 ? "pass" : "fail", 0);
    kui_element_release(result);
}

/* ---- Script value helpers (input values are copied by KUI; outputs are borrowed) ---------- */

static KuiCScriptValue sv_nil(void) {
    KuiCScriptValue value;
    memset(&value, 0, sizeof value);
    value.type = KUI_C_SCRIPT_NIL;
    return value;
}

static KuiCScriptValue sv_bool(int on) {
    KuiCScriptValue value = sv_nil();
    value.type = KUI_C_SCRIPT_BOOL;
    value.bool_value = on ? 1 : 0;
    return value;
}

static KuiCScriptValue sv_int(int64_t number) {
    KuiCScriptValue value = sv_nil();
    value.type = KUI_C_SCRIPT_INT;
    value.int_value = number;
    return value;
}

static KuiCScriptValue sv_float(double number) {
    KuiCScriptValue value = sv_nil();
    value.type = KUI_C_SCRIPT_FLOAT;
    value.float_value = number;
    return value;
}

static KuiCScriptValue sv_string(const char* text) {
    KuiCScriptValue value = sv_nil();
    value.type = KUI_C_SCRIPT_STRING;
    value.string_value = text;
    value.string_len = strlen(text);
    return value;
}

static KuiCScriptValue sv_function(KuiScriptFunction* function) {
    KuiCScriptValue value = sv_nil();
    value.type = KUI_C_SCRIPT_FUNCTION;
    value.function_value = function;
    return value;
}

/* A number of either Lua subtype as a double. */
static double sv_number(const KuiCScriptValue* value) {
    if (value->type == KUI_C_SCRIPT_INT) return (double)value->int_value;
    if (value->type == KUI_C_SCRIPT_FLOAT) return value->float_value;
    return 0.0;
}

/* Copies a string result (empty for any other type). */
static void sv_text(const KuiCScriptValue* value, char* buf, size_t capacity) {
    buf[0] = '\0';
    if (value->type == KUI_C_SCRIPT_STRING && value->string_value) {
        snprintf(buf, capacity, "%.*s", (int)value->string_len, value->string_value);
    }
}

static void last_error(char* buf, size_t capacity) {
    size_t length = 0;
    buf[0] = '\0';
    kui_script_engine_get_last_error(g_script, buf, capacity, &length);
}

/* Evaluates a chunk; returns 1 when it ran, its first return value in *out. */
static int eval(const char* code, KuiCScriptValue* out) {
    int success = 0;
    *out = sv_nil();
    if (kui_script_engine_eval_value(g_script, code, 0, "tour", 0, &success, out) != KUI_OK) return 0;
    return success;
}

static int eval_bool(const char* code) {
    KuiCScriptValue value;
    return eval(code, &value) && value.type == KUI_C_SCRIPT_BOOL && value.bool_value;
}

static int call(const char* name, const KuiCScriptValue* args, size_t count, KuiCScriptValue* out) {
    int success = 0;
    *out = sv_nil();
    if (kui_script_engine_call_function(g_script, name, 0, args, count, &success, out) != KUI_OK)
        return 0;
    return success;
}

static int64_t frame_count(void) {
    KuiCScriptValue value;
    return call("getFrameCount", NULL, 0, &value) ? value.int_value : -1;
}

/* ---- Host functions Lua can call (register_function / function_create) ------------------ */

static int g_reports;

/* host_report(frames, fps): Lua's render loop reports progress; the host shows it. */
static KuiStatus host_report(const KuiCScriptValue* args, size_t count, KuiCScriptValue* out,
                             void* user_data) {
    char text[96];
    (void)out;
    (void)user_data;
    g_reports++;
    snprintf(text, sizeof text, "frames=%.0f | fps=%.0f", count > 0 ? sv_number(&args[0]) : 0.0,
             count > 1 ? sv_number(&args[1]) : 0.0);
    kuex_set_text(g_doc, "gl-report", text);
    return KUI_OK;
}

/* host_kind(v): names the kind of value the host received. */
static KuiStatus host_kind(const KuiCScriptValue* args, size_t count, KuiCScriptValue* out,
                           void* user_data) {
    static const char* const names[] = {"nil", "bool", "int", "float", "string", "function",
                                        "userdata"};
    int kind = count > 0 ? (int)args[0].type : KUI_C_SCRIPT_NIL;
    (void)user_data;
    *out = sv_string(kind >= 0 && kind <= KUI_C_SCRIPT_USERDATA ? names[kind] : "?");
    return KUI_OK;
}

/* host_fail(): a host error becomes a Lua error with this message. */
static KuiStatus host_fail(const KuiCScriptValue* args, size_t count, KuiCScriptValue* out,
                           void* user_data) {
    (void)args;
    (void)count;
    (void)user_data;
    *out = sv_string("host says no");
    return KUI_ERR_INVALID_ARG;
}

/* host_double(x): a host function handed to Lua as a value. */
static KuiStatus host_double(const KuiCScriptValue* args, size_t count, KuiCScriptValue* out,
                             void* user_data) {
    (void)user_data;
    if (count < 1) return KUI_ERR_INVALID_ARG;
    *out = args[0].type == KUI_C_SCRIPT_INT ? sv_int(args[0].int_value * 2)
                                            : sv_float(sv_number(&args[0]) * 2);
    return KUI_OK;
}

static void register_host_functions(void) {
    kui_script_engine_register_function(g_script, "host_report", 0, host_report, NULL, NULL);
    kui_script_engine_register_function(g_script, "host_kind", 0, host_kind, NULL, NULL);
    kui_script_engine_register_function(g_script, "host_fail", 0, host_fail, NULL, NULL);
}

/* ---- Globals: every value kind through set_global / get_global ---------------------------- */

static void check_globals(void) {
    KuiCScriptValue value;
    KuiCScriptValue in;
    char text[64];

    in = sv_nil();
    kui_script_engine_set_global(g_script, "t_nil", 0, &in);
    kui_script_engine_get_global(g_script, "t_nil", 0, &value);
    {
        KuiCScriptValue missing;
        kui_script_engine_get_global(g_script, "no_such_global", 0, &missing);
        check("global.nil", value.type == KUI_C_SCRIPT_NIL && missing.type == KUI_C_SCRIPT_NIL,
              "types %d / %d", value.type, missing.type);
    }

    in = sv_bool(1);
    kui_script_engine_set_global(g_script, "t_bool", 0, &in);
    kui_script_engine_get_global(g_script, "t_bool", 0, &value);
    check("global.bool", value.type == KUI_C_SCRIPT_BOOL && value.bool_value == 1 &&
          eval_bool("return t_bool == true"), "type %d value %d", value.type, value.bool_value);

    in = sv_int(42);
    kui_script_engine_set_global(g_script, "t_int", 0, &in);
    kui_script_engine_get_global(g_script, "t_int", 0, &value);
    check("global.int", value.type == KUI_C_SCRIPT_INT && value.int_value == 42 &&
          eval_bool("return math.type(t_int) == 'integer'"), "type %d value %lld", value.type,
          (long long)value.int_value);

    in = sv_float(2.5);
    kui_script_engine_set_global(g_script, "t_float", 0, &in);
    kui_script_engine_get_global(g_script, "t_float", 0, &value);
    check("global.float", value.type == KUI_C_SCRIPT_FLOAT && value.float_value == 2.5 &&
          eval_bool("return math.type(t_float) == 'float'"), "type %d value %g", value.type,
          value.float_value);

    in = sv_string("h\xc3\xa9llo, KUI");
    kui_script_engine_set_global(g_script, "t_str", 0, &in);
    kui_script_engine_get_global(g_script, "t_str", 0, &value);
    sv_text(&value, text, sizeof text);
    check("global.string", strcmp(text, "h\xc3\xa9llo, KUI") == 0 && value.string_len == 11 &&
          eval_bool("return #t_str == 11"), "text '%s' length %zu", text, value.string_len);

    /* The page's own globals are what the scene reads every frame. */
    kui_script_engine_get_global(g_script, "gl_ready", 0, &value);
    {
        char status[96];
        kuex_get_text(g_doc, "gl-status", status, sizeof status);
        check("webgl.page_ready", value.type == KUI_C_SCRIPT_BOOL && value.bool_value,
              "gl_ready type %d, #gl-status '%s'", value.type, status);
    }
}

/* ---- Calls: host -> Lua (call_function) and Lua -> host (register_function) --------------- */

static void check_calls(void) {
    KuiCScriptValue value;
    KuiCScriptValue arg;
    char text[160];
    int ok;

    arg = sv_float(1.5);
    ok = call("setSpeed", &arg, 1, &value);
    {
        KuiCScriptValue speed;
        kui_script_engine_get_global(g_script, "speed", 0, &speed);
        check("call_function.setSpeed", ok && sv_number(&value) == 1.5 && sv_number(&speed) == 1.5,
              "ok %d returned %g, global speed %g", ok, sv_number(&value), sv_number(&speed));
    }
    arg = sv_float(1.0);
    call("setSpeed", &arg, 1, &value);

    {
        KuiCScriptValue first;
        KuiCScriptValue second;
        call("togglePause", NULL, 0, &first);
        call("togglePause", NULL, 0, &second);
        check("call_function.togglePause", first.type == KUI_C_SCRIPT_BOOL && first.bool_value == 1 &&
              second.type == KUI_C_SCRIPT_BOOL && second.bool_value == 0,
              "first %d/%d second %d/%d", first.type, first.bool_value, second.type,
              second.bool_value);
    }

    ok = call("noSuchFunction", NULL, 0, &value);
    last_error(text, sizeof text);
    check("get_last_error.missing_function", !ok && strstr(text, "noSuchFunction") != NULL,
          "ok %d error '%s'", ok, text);
    ok = call("getFrameCount", NULL, 0, &value);
    last_error(text, sizeof text);
    check("get_last_error.cleared", ok && value.type == KUI_C_SCRIPT_INT && text[0] == '\0',
          "ok %d type %d error '%s'", ok, value.type, text);

    register_host_functions();
    ok = eval("return hostKinds()", &value);
    sv_text(&value, text, sizeof text);
    check("register_function.kinds", ok && strcmp(text, "nil,bool,int,float,string,function") == 0,
          "ok %d kinds '%s'", ok, text);

    ok = eval("local ok, e = pcall(host_fail) return (not ok) and tostring(e) or 'no error'", &value);
    sv_text(&value, text, sizeof text);
    check("register_function.error", ok && strstr(text, "host says no") != NULL, "ok %d '%s'", ok,
          text);
}

/* ---- Function values: create, call, retain, release (host and Lua functions) -------------- */

static KuiScriptFunction* g_counter; /* a Lua closure the "Lua counter++" button keeps calling */
static KuiScriptFunction* g_double;  /* host_double, also stored in the Lua global host_double */

static void check_functions(void) {
    KuiScriptFunction* copy = NULL;
    KuiCScriptValue value;
    KuiCScriptValue arg;
    int success = 0;
    int ok;

    kui_script_function_create(g_script, host_double, NULL, NULL, &g_double);
    value = sv_function(g_double);
    kui_script_engine_set_global(g_script, "host_double", 0, &value);
    ok = eval("return applyTwice(host_double, 5)", &value);
    check("function_create.as_lua_value", g_double && ok && value.int_value == 20,
          "handle %d ok %d value %lld", g_double != NULL, ok, (long long)value.int_value);

    arg = sv_int(21);
    value = sv_nil();
    if (g_double) kui_script_function_call(g_double, &arg, 1, &success, &value);
    check("function_call.host", success && value.type == KUI_C_SCRIPT_INT && value.int_value == 42,
          "success %d type %d value %lld", success, value.type, (long long)value.int_value);

    /* A second owned handle outlives the first; Lua's reference is independent of both. */
    {
        KuiStatus retained = g_double ? kui_script_function_retain(g_double, &copy) : KUI_ERR_INVALID_ARG;
        KuiStatus released;
        success = 0;
        arg = sv_int(4);
        if (copy) kui_script_function_call(copy, &arg, 1, &success, &value);
        released = copy ? kui_script_function_release(copy) : KUI_ERR_INVALID_ARG;
        check("function_retain_release", retained == KUI_OK && success && value.int_value == 8 &&
              released == KUI_OK && eval_bool("return host_double(3) == 6"),
              "retain %d call %d value %lld release %d", retained, success,
              (long long)value.int_value, released);
    }

    /* get_global returns a borrowed Lua function; retain it to call it later. */
    {
        KuiScriptFunction* make = NULL;
        KuiCScriptValue counter;
        int64_t got[3] = {0, 0, 0};
        int index;
        kui_script_engine_get_global(g_script, "makeCounter", 0, &value);
        if (value.type == KUI_C_SCRIPT_FUNCTION) kui_script_function_retain(value.function_value, &make);
        counter = sv_nil();
        if (make) kui_script_function_call(make, NULL, 0, &success, &counter);
        if (counter.type == KUI_C_SCRIPT_FUNCTION)
            kui_script_function_retain(counter.function_value, &g_counter);
        for (index = 0; index < 3 && g_counter; ++index) {
            kui_script_function_call(g_counter, NULL, 0, &success, &value);
            got[index] = value.int_value;
        }
        check("get_global.function", make && g_counter && got[0] == 1 && got[1] == 2 && got[2] == 3,
              "makeCounter %d counter %d values %lld,%lld,%lld", make != NULL, g_counter != NULL,
              (long long)got[0], (long long)got[1], (long long)got[2]);
        if (make) kui_script_function_release(make);
    }

    /* A Lua error inside a Lua function yields nil (kui_c.h, as in native C++). */
    {
        KuiScriptFunction* fails = NULL;
        kui_script_engine_get_global(g_script, "luaFails", 0, &value);
        if (value.type == KUI_C_SCRIPT_FUNCTION) kui_script_function_retain(value.function_value, &fails);
        value = sv_int(-1);
        if (fails) kui_script_function_call(fails, NULL, 0, &success, &value);
        check("function_call.lua_error_is_nil", fails && value.type == KUI_C_SCRIPT_NIL,
              "handle %d type %d", fails != NULL, value.type);
        if (fails) kui_script_function_release(fails);
    }
}

/* ---- Limits and resource usage ----------------------------------------------------------- */

/* ---- Timers: the engine runs due timers only when the host ticks it --------------------------- */

static void check_timers(void) {
    KuiCScriptValue value;
    double now = 0.0;
    int scheduled, before, after;

    /* A timer is due `delay` after the engine's clock when the script scheduled it; ticking the
     * current time first makes the test independent of any earlier tick. */
    kui_platform_now(&now);
    kui_script_engine_tick_timers(g_script, now);
    scheduled = eval("tour_timer_fired = false; setTimeout(function() tour_timer_fired = true end, 50)", &value);
    kui_script_engine_tick_timers(g_script, now + 10.0);
    before = eval_bool("return tour_timer_fired == false");
    kui_script_engine_tick_timers(g_script, now + 100.0);
    after = eval_bool("return tour_timer_fired == true");
    check("timers.tick_timers", scheduled && before && after, "scheduled %d, not fired early %d, fired after the tick %d",
          scheduled, before, after);
}

static void check_limits(void) {
    size_t memory = 0, memory_limit = 0, memory_limit_now = 0;
    uint64_t instructions = 0, instructions_now = 0;
    KuiCScriptValue value;
    char text[200];
    int ok;

    kui_script_engine_get_resource_usage(g_script, &memory, &memory_limit, &instructions);
    check("get_resource_usage.defaults", memory > 0 && memory_limit == 256u * 1024 * 1024 &&
          instructions == 100000000u, "memory %zu limit %zu instructions %llu", memory,
          memory_limit, (unsigned long long)instructions);

    kui_script_engine_set_instruction_limit(g_script, 10000);
    kui_script_engine_get_resource_usage(g_script, NULL, NULL, &instructions_now);
    ok = eval("while true do end", &value);
    last_error(text, sizeof text);
    check("set_instruction_limit.stops_loop", instructions_now == 10000 && !ok &&
          strstr(text, "instruction limit") != NULL, "limit %llu ok %d error '%s'",
          (unsigned long long)instructions_now, ok, text);
    kui_script_engine_set_instruction_limit(g_script, instructions);
    kui_script_engine_get_resource_usage(g_script, NULL, NULL, &instructions_now);
    ok = eval("local n = 0 for i = 1, 100000 do n = n + i end return n", &value);
    check("set_instruction_limit.restore", instructions_now == instructions && ok &&
          value.int_value == 5000050000LL, "limit %llu ok %d value %lld",
          (unsigned long long)instructions_now, ok, (long long)value.int_value);

    kui_script_engine_set_memory_limit(g_script, memory + 1024 * 1024);
    kui_script_engine_get_resource_usage(g_script, NULL, &memory_limit_now, NULL);
    ok = eval("local s = string.rep('x', 16 * 1024 * 1024) return #s", &value);
    last_error(text, sizeof text);
    check("set_memory_limit.refuses", memory_limit_now == memory + 1024 * 1024 && !ok &&
          strstr(text, "memory") != NULL, "limit %zu ok %d error '%s'", memory_limit_now, ok, text);
    kui_script_engine_set_memory_limit(g_script, memory_limit);
    kui_script_engine_get_resource_usage(g_script, NULL, &memory_limit_now, NULL);
    ok = eval("local s = string.rep('x', 16 * 1024 * 1024) local n = #s s = nil "
              "collectgarbage() return n", &value);
    check("set_memory_limit.restore", memory_limit_now == memory_limit && ok &&
          value.int_value == 16 * 1024 * 1024, "limit %zu ok %d value %lld", memory_limit_now, ok,
          (long long)value.int_value);
}

/* ---- print, eval_file, document binding --------------------------------------------------- */

static char g_printed[256];
static int g_capture_released;
static int g_prints;

static void capture_print(const char* message, size_t length, void* user_data) {
    (void)user_data;
    snprintf(g_printed, sizeof g_printed, "%.*s", (int)length, message);
}

static void capture_released(void* user_data) {
    (void)user_data;
    g_capture_released++;
}

/* The handler kept after the tour: shows Lua's print() output in #print-out. */
static void show_print(const char* message, size_t length, void* user_data) {
    char text[256];
    (void)user_data;
    snprintf(text, sizeof text, "#%d %.*s", ++g_prints, (int)length, message);
    kuex_set_text(g_doc, "print-out", text);
}

/* Writes a small Lua file into the temp directory; returns 1 on success. */
static int write_lua_file(char* path, size_t capacity) {
    const char* dir = getenv("TEMP");
    FILE* file;
    if (!dir || !*dir) dir = getenv("TMPDIR");
    if (!dir || !*dir) dir = "/tmp";
    snprintf(path, capacity, "%s/kui_api_script_webgl_c.lua", dir);
    file = fopen(path, "wb");
    if (!file) return 0;
    fputs("from_file = 'loaded'\nreturn 6 * 7\n", file);
    fclose(file);
    return 1;
}

static void check_print_file_binding(void) {
    KuiCScriptValue value;
    char path[512];
    char text[200];
    int ok;

    kui_script_engine_set_print_handler(g_script, capture_print, NULL, capture_released);
    eval("print('hello', 42)", &value);
    check("set_print_handler.captures", strcmp(g_printed, "hello\t42") == 0, "captured '%s'",
          g_printed);
    /* kui_c.h: replacing the handler releases the old one. */
    kui_script_engine_set_print_handler(g_script, show_print, NULL, NULL);
    check("set_print_handler.release", g_capture_released == 1, "release_fn calls %d",
          g_capture_released);

    ok = write_lua_file(path, sizeof path) &&
         kui_script_engine_eval_file(g_script, path, 0, &ok, &value) == KUI_OK && ok;
    {
        KuiCScriptValue loaded;
        char from[32];
        kui_script_engine_get_global(g_script, "from_file", 0, &loaded);
        sv_text(&loaded, from, sizeof from);
        check("eval_file.runs", ok && value.type == KUI_C_SCRIPT_INT && value.int_value == 42 &&
              strcmp(from, "loaded") == 0, "ok %d value %lld from_file '%s'", ok,
              (long long)value.int_value, from);
    }
    remove(path);
    ok = 1;
    kui_script_engine_eval_file(g_script, "no/such/file.lua", 0, &ok, &value);
    last_error(text, sizeof text);
    check("eval_file.missing", !ok && text[0] != '\0', "ok %d error '%s'", ok, text);

    kui_script_engine_unbind_document(g_script);
    ok = eval_bool("return document == nil");
    check("unbind_document", ok, "document still bound");
    kui_script_engine_bind_document(g_script, g_doc);
    ok = eval_bool("return document:getElementById('gl-canvas') ~= nil");
    check("bind_document.again", ok, "document not usable after re-binding");
}

/* ---- Asynchronous part: the WebGL scene advances, pauses and resumes ------------------- */

static int64_t g_frames_mark;

static void phase_resumed(void* user_data) {
    int64_t now = frame_count();
    (void)user_data;
    check("webgl.resume", now > g_frames_mark, "frames %lld after resume, %lld while paused",
          (long long)now, (long long)g_frames_mark);
    finish();
}

static void phase_paused(void* user_data) {
    KuiCScriptValue value;
    int64_t now = frame_count();
    KuiTimerId id;
    (void)user_data;
    check("webgl.pause_stops", now == g_frames_mark, "frames %lld -> %lld while paused",
          (long long)g_frames_mark, (long long)now);
    call("togglePause", NULL, 0, &value);
    kui_platform_set_timeout(phase_resumed, NULL, 400, &id);
}

static void phase_pausing(void* user_data) {
    KuiTimerId id;
    (void)user_data;
    g_frames_mark = frame_count();
    kui_platform_set_timeout(phase_paused, NULL, 300, &id);
}

static void phase_running(void* user_data) {
    KuiCScriptValue value;
    KuiCScriptValue paused;
    char report[96];
    int64_t now = frame_count();
    KuiTimerId id;
    (void)user_data;
    check("webgl.frames_advance", now > g_frames_mark, "frames %lld -> %lld",
          (long long)g_frames_mark, (long long)now);
    kuex_get_text(g_doc, "gl-report", report, sizeof report);
    check("register_function.host_report", g_reports > 0 && strncmp(report, "frames=", 7) == 0,
          "host_report calls %d, #gl-report '%s'", g_reports, report);
    call("togglePause", NULL, 0, &value);
    kui_script_engine_get_global(g_script, "paused", 0, &paused);
    check("webgl.paused_global", paused.type == KUI_C_SCRIPT_BOOL && paused.bool_value,
          "paused type %d value %d", paused.type, paused.bool_value);
    /* Let a frame already in flight land before taking the paused mark. */
    kui_platform_set_timeout(phase_pausing, NULL, 100, &id);
}

/* ---- Panel buttons ----------------------------------------------------------------------- */

static void on_gl_pause(KuiEvent* event, void* user_data) {
    KuiCScriptValue value;
    (void)event;
    (void)user_data;
    call("togglePause", NULL, 0, &value);
    kuex_set_text(g_doc, "gl-state", value.bool_value ? "paused" : "running");
}

static void on_gl_speed(KuiEvent* event, void* user_data) {
    KuiCScriptValue arg = sv_float(3.0);
    KuiCScriptValue value;
    char text[48];
    (void)event;
    (void)user_data;
    call("setSpeed", &arg, 1, &value);
    snprintf(text, sizeof text, "speed=%g", sv_number(&value));
    kuex_set_text(g_doc, "gl-out", text);
}

static void on_gl_tint(KuiEvent* event, void* user_data) {
    static const char* const tints[] = {"sky", "rose", "lime"};
    KuiCScriptValue value;
    char current[16];
    char text[48];
    int index;
    (void)event;
    (void)user_data;
    kui_script_engine_get_global(g_script, "tint", 0, &value);
    sv_text(&value, current, sizeof current);
    for (index = 0; index < 3 && strcmp(tints[index], current) != 0; ++index) {
    }
    value = sv_string(tints[(index + 1) % 3]);
    kui_script_engine_set_global(g_script, "tint", 0, &value);
    snprintf(text, sizeof text, "tint=%s", tints[(index + 1) % 3]);
    kuex_set_text(g_doc, "gl-out", text);
}

static void on_gl_scale(KuiEvent* event, void* user_data) {
    KuiCScriptValue value;
    char text[48];
    double next;
    (void)event;
    (void)user_data;
    kui_script_engine_get_global(g_script, "scale", 0, &value);
    next = sv_number(&value) < 0.65 ? 0.8 : 0.5;
    value = sv_float(next);
    kui_script_engine_set_global(g_script, "scale", 0, &value);
    snprintf(text, sizeof text, "scale=%g", next);
    kuex_set_text(g_doc, "gl-out", text);
}

static void on_gl_frames(KuiEvent* event, void* user_data) {
    char text[48];
    (void)event;
    (void)user_data;
    snprintf(text, sizeof text, "frames=%lld", (long long)frame_count());
    kuex_set_text(g_doc, "gl-out", text);
}

static void on_val_roundtrip(KuiEvent* event, void* user_data) {
    static const char* const names[] = {"nil", "bool", "int", "float", "string", "function",
                                        "userdata"};
    static const char* const globals[] = {"t_nil", "t_bool", "t_int", "t_float", "t_str",
                                          "makeCounter"};
    KuiCScriptValue value;
    char text[160] = "";
    size_t index;
    (void)event;
    (void)user_data;
    for (index = 0; index < sizeof globals / sizeof globals[0]; ++index) {
        size_t used = strlen(text);
        kui_script_engine_get_global(g_script, globals[index], 0, &value);
        snprintf(text + used, sizeof text - used, "%s%s", index ? "," : "", names[value.type]);
    }
    kuex_set_text(g_doc, "val-out", text);
}

static void on_val_kinds(KuiEvent* event, void* user_data) {
    KuiCScriptValue value;
    char text[96];
    char out[128];
    (void)event;
    (void)user_data;
    eval("return hostKinds()", &value);
    sv_text(&value, text, sizeof text);
    snprintf(out, sizeof out, "host saw: %s", text);
    kuex_set_text(g_doc, "val-out", out);
}

static void on_val_double(KuiEvent* event, void* user_data) {
    KuiCScriptValue arg = sv_int(21);
    KuiCScriptValue value = sv_nil();
    int success = 0;
    char text[64];
    (void)event;
    (void)user_data;
    if (g_double) kui_script_function_call(g_double, &arg, 1, &success, &value);
    snprintf(text, sizeof text, "host_double(21) = %lld", (long long)value.int_value);
    kuex_set_text(g_doc, "val-out", text);
}

static void on_val_counter(KuiEvent* event, void* user_data) {
    KuiCScriptValue value = sv_nil();
    int success = 0;
    char text[48];
    (void)event;
    (void)user_data;
    if (g_counter) kui_script_function_call(g_counter, NULL, 0, &success, &value);
    snprintf(text, sizeof text, "counter=%lld", (long long)value.int_value);
    kuex_set_text(g_doc, "val-out", text);
}

static void on_lim_loop(KuiEvent* event, void* user_data) {
    uint64_t saved = 0;
    KuiCScriptValue value;
    char text[200];
    char out[240];
    (void)event;
    (void)user_data;
    kui_script_engine_get_resource_usage(g_script, NULL, NULL, &saved);
    kui_script_engine_set_instruction_limit(g_script, 10000);
    eval("while true do end", &value);
    last_error(text, sizeof text);
    kui_script_engine_set_instruction_limit(g_script, saved);
    snprintf(out, sizeof out, "stopped: %s", text);
    kuex_set_text(g_doc, "lim-out", out);
}

static void on_lim_mem(KuiEvent* event, void* user_data) {
    size_t memory = 0, saved = 0;
    KuiCScriptValue value;
    char text[200];
    char out[240];
    (void)event;
    (void)user_data;
    kui_script_engine_get_resource_usage(g_script, &memory, &saved, NULL);
    kui_script_engine_set_memory_limit(g_script, memory + 1024 * 1024);
    eval("local s = string.rep('x', 16 * 1024 * 1024) return #s", &value);
    last_error(text, sizeof text);
    kui_script_engine_set_memory_limit(g_script, saved);
    snprintf(out, sizeof out, "refused: %s", text);
    kuex_set_text(g_doc, "lim-out", out);
}

static void on_lim_usage(KuiEvent* event, void* user_data) {
    size_t memory = 0, limit = 0;
    uint64_t instructions = 0;
    char text[128];
    (void)event;
    (void)user_data;
    kui_script_engine_get_resource_usage(g_script, &memory, &limit, &instructions);
    snprintf(text, sizeof text, "memory=%zu KiB | limit=%zu MiB | instructions=%llu",
             memory / 1024, limit / (1024 * 1024), (unsigned long long)instructions);
    kuex_set_text(g_doc, "lim-out", text);
}

static void on_lim_error(KuiEvent* event, void* user_data) {
    KuiCScriptValue value;
    char text[200];
    char out[240];
    (void)event;
    (void)user_data;
    call("noSuchFunction", NULL, 0, &value);
    last_error(text, sizeof text);
    snprintf(out, sizeof out, "last error: %s", text);
    kuex_set_text(g_doc, "lim-out", out);
}

static void on_io_print(KuiEvent* event, void* user_data) {
    KuiCScriptValue value;
    (void)event;
    (void)user_data;
    eval("print('hello from Lua', frames > 0)", &value);
}

static void on_io_file(KuiEvent* event, void* user_data) {
    KuiCScriptValue value = sv_nil();
    char path[512];
    char text[64];
    int ok = 0;
    (void)event;
    (void)user_data;
    if (write_lua_file(path, sizeof path)) {
        kui_script_engine_eval_file(g_script, path, 0, &ok, &value);
        remove(path);
    }
    snprintf(text, sizeof text, "eval_file -> %lld", ok ? (long long)value.int_value : -1LL);
    kuex_set_text(g_doc, "io-out", text);
}

static void on_io_rebind(KuiEvent* event, void* user_data) {
    int unbound;
    int rebound;
    char text[96];
    (void)event;
    (void)user_data;
    kui_script_engine_unbind_document(g_script);
    unbound = eval_bool("return document == nil");
    kui_script_engine_bind_document(g_script, g_doc);
    rebound = eval_bool("return document ~= nil");
    snprintf(text, sizeof text, "unbound: document=%s | rebound: document=%s",
             unbound ? "nil" : "set", rebound ? "set" : "nil");
    kuex_set_text(g_doc, "io-out", text);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    int failed = 0;
    KuiTimerId id;
    g_doc = host->document;
    g_script = host->script;

    check_globals();
    check_calls();
    check_functions();
    check_limits();
    check_timers();
    check_print_file_binding();
    /* The rest needs frames: the scene must advance, stop while paused and resume. */
    g_frames_mark = frame_count();
    kui_platform_set_timeout(phase_running, NULL, 600, &id);

    failed |= kuex_on(g_doc, "btn-gl-pause", "click", on_gl_pause, NULL);
    failed |= kuex_on(g_doc, "btn-gl-speed", "click", on_gl_speed, NULL);
    failed |= kuex_on(g_doc, "btn-gl-tint", "click", on_gl_tint, NULL);
    failed |= kuex_on(g_doc, "btn-gl-scale", "click", on_gl_scale, NULL);
    failed |= kuex_on(g_doc, "btn-gl-frames", "click", on_gl_frames, NULL);
    failed |= kuex_on(g_doc, "btn-val-roundtrip", "click", on_val_roundtrip, NULL);
    failed |= kuex_on(g_doc, "btn-val-kinds", "click", on_val_kinds, NULL);
    failed |= kuex_on(g_doc, "btn-val-double", "click", on_val_double, NULL);
    failed |= kuex_on(g_doc, "btn-val-counter", "click", on_val_counter, NULL);
    failed |= kuex_on(g_doc, "btn-lim-loop", "click", on_lim_loop, NULL);
    failed |= kuex_on(g_doc, "btn-lim-mem", "click", on_lim_mem, NULL);
    failed |= kuex_on(g_doc, "btn-lim-usage", "click", on_lim_usage, NULL);
    failed |= kuex_on(g_doc, "btn-lim-error", "click", on_lim_error, NULL);
    failed |= kuex_on(g_doc, "btn-io-print", "click", on_io_print, NULL);
    failed |= kuex_on(g_doc, "btn-io-file", "click", on_io_file, NULL);
    failed |= kuex_on(g_doc, "btn-io-rebind", "click", on_io_rebind, NULL);
    return failed;
}
