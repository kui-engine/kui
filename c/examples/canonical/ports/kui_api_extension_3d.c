/* API tour: dynamic extensions and a host-driven 3D scene (examples/api_tours/extension_3d).
 * The host builds a scene in <kui-3d-view> through kui_document_extension_invoke (meshes,
 * lights, materials, a glTF model, an orbit camera driven by mouse events), inspects the
 * kui_ext_3d extension (state, perf stats, watch), unloads and reloads it and rebuilds the
 * scene, and reads every invoke result object. One #tour-log row per check; #tour-result ends
 * as "PASS n/n". The panel buttons repeat the calls by hand. */
#include "kui_c_example_dom.h"

#include <stdarg.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#define EXT_3D_FILE "kui_ext_3d.dll"
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#define EXT_3D_FILE "libkui_ext_3d.dylib"
#else
#include <unistd.h>
#define EXT_3D_FILE "libkui_ext_3d.so"
#endif

#define TAG_3D "kui-3d-view"

static KuiDocument* g_doc;

/* ---- Self-check bookkeeping: count, log one <tr> per check, publish the verdict ----------
 * check():  a documented behaviour; a failure makes the tour FAIL.
 * known():  a documented behaviour the engine currently gets wrong (reported bug). A failure is
 *           logged as "KNOWN BUG" and kept out of the verdict; once it passes it counts as ok. */

static int g_total;
static int g_failed;
static int g_known;
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
    KuiElement* result = kuex_by_id(g_doc, "tour-result");
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

/* ---- Paths: the extension DLL and the bundled glTF model sit next to the executable -------- */

static char g_exe_dir[1024];
static char g_ext_path[1100];
static char g_tank_path[1100];

static void find_paths(void) {
#ifdef _WIN32
    DWORD length = GetModuleFileNameA(NULL, g_exe_dir, (DWORD)sizeof g_exe_dir);
    if (length == 0 || length >= sizeof g_exe_dir) length = 0;
    g_exe_dir[length] = '\0';
#elif defined(__APPLE__)
    uint32_t length = (uint32_t)sizeof g_exe_dir;
    if (_NSGetExecutablePath(g_exe_dir, &length) != 0) g_exe_dir[0] = '\0';
#else
    ssize_t length = readlink("/proc/self/exe", g_exe_dir, sizeof g_exe_dir - 1);
    g_exe_dir[length > 0 ? length : 0] = '\0';
#endif
    {
        char* slash = strrchr(g_exe_dir, '\\');
        char* forward = strrchr(g_exe_dir, '/');
        if (forward && (!slash || forward > slash)) slash = forward;
        if (slash) *slash = '\0';
    }
    snprintf(g_ext_path, sizeof g_ext_path, "%s/%s", g_exe_dir, EXT_3D_FILE);
    snprintf(g_tank_path, sizeof g_tank_path, "%s/assets/native_3d/stylized_tank.glb", g_exe_dir);
}

/* ---- Extension calls: typed arguments in, a result object out -------------------------------
 * kui_document_extension_invoke returns KUI_OK whenever it produced a result object; whether the
 * call itself worked is the result's status (0 ok, 3 unknown method, 4 wrong argument count). */

static KuiCExtensionValue v_int(long long value) {
    KuiCExtensionValue result;
    memset(&result, 0, sizeof result);
    result.type = KUI_C_EXT_INT;
    result.int_value = value;
    return result;
}

static KuiCExtensionValue v_num(double value) {
    KuiCExtensionValue result;
    memset(&result, 0, sizeof result);
    result.type = KUI_C_EXT_FLOAT;
    result.float_value = value;
    return result;
}

static KuiCExtensionValue v_str(const char* value) {
    KuiCExtensionValue result;
    memset(&result, 0, sizeof result);
    result.type = KUI_C_EXT_STRING;
    result.string_value = value;
    result.string_len = strlen(value);
    return result;
}

/* Invokes `method` on the current #view; NULL when there is no view or the call was rejected. */
static KuiExtensionResult* call3d(const char* method, const KuiCExtensionValue* args, size_t count,
                                  size_t max_results) {
    KuiElement* view = kuex_by_id(g_doc, "view");
    KuiExtensionResult* result = NULL;
    if (!view) return NULL;
    if (kui_document_extension_invoke(g_doc, view, method, 0, args, count, max_results,
                                      &result) != KUI_OK) {
        result = NULL;
    }
    kui_element_release(view);
    return result;
}

static int result_status(KuiExtensionResult* result) {
    int status = -1;
    if (result) kui_extension_result_get_status(result, &status);
    return status;
}

static size_t result_count(KuiExtensionResult* result) {
    size_t count = 0;
    if (result) kui_extension_result_get_count(result, &count);
    return count;
}

static void result_error(KuiExtensionResult* result, char* buf, size_t capacity) {
    size_t length = 0;
    buf[0] = '\0';
    if (result) kui_extension_result_get_error(result, buf, capacity, &length);
}

/* First result as an integer (entity / asset handle); 0 when the call failed. */
static long long call_int(const char* method, const KuiCExtensionValue* args, size_t count) {
    KuiExtensionResult* result = call3d(method, args, count, 1);
    KuiCExtensionValue value;
    long long output = 0;
    memset(&value, 0, sizeof value);
    if (result_status(result) == 0 && result_count(result) >= 1 &&
        kui_extension_result_get_value(result, 0, &value) == KUI_OK &&
        value.type == KUI_C_EXT_INT) {
        output = value.int_value;
    }
    if (result) kui_extension_result_release(result);
    return output;
}

/* 1 when the call succeeded and its first result (if any) is true. */
static int call_ok(const char* method, const KuiCExtensionValue* args, size_t count) {
    KuiExtensionResult* result = call3d(method, args, count, 1);
    KuiCExtensionValue value;
    int ok = result_status(result) == 0;
    memset(&value, 0, sizeof value);
    if (ok && result_count(result) >= 1 &&
        kui_extension_result_get_value(result, 0, &value) == KUI_OK &&
        value.type == KUI_C_EXT_BOOL) {
        ok = value.bool_value != 0;
    }
    if (result) kui_extension_result_release(result);
    return ok;
}

/* "status=3 count=0 error=..." or the integer values, for the result panel. */
static void describe_result(KuiExtensionResult* result, char* out, size_t capacity) {
    char error[160];
    size_t index;
    size_t used;
    size_t count = result_count(result);
    KuiCExtensionValue value;
    snprintf(out, capacity, "status=%d count=%u", result_status(result), (unsigned)count);
    for (index = 0; index < count; ++index) {
        memset(&value, 0, sizeof value);
        if (kui_extension_result_get_value(result, index, &value) != KUI_OK) break;
        used = strlen(out);
        snprintf(out + used, capacity - used, "%s%lld", index ? "," : " values=",
                 value.type == KUI_C_EXT_INT ? (long long)value.int_value : 0LL);
    }
    result_error(result, error, sizeof error);
    if (error[0]) {
        used = strlen(out);
        snprintf(out + used, capacity - used, " error=%s", error);
    }
}

/* ---- Small trigonometry (degrees) without libm: the C examples link nothing but kui_c ------- */

static double wrap_rad(double x) {
    const double pi = 3.14159265358979323846;
    while (x > pi) x -= 2.0 * pi;
    while (x < -pi) x += 2.0 * pi;
    return x;
}

static double sin_rad(double x) {
    double term, sum;
    int n;
    x = wrap_rad(x);
    term = x;
    sum = x;
    for (n = 1; n < 12; ++n) {
        term *= -x * x / ((2.0 * n) * (2.0 * n + 1.0));
        sum += term;
    }
    return sum;
}

static double cos_rad(double x) { return sin_rad(x + 1.57079632679489661923); }

#define DEG(d) ((d) * 3.14159265358979323846 / 180.0)

/* ---- Orbit camera: azimuth / elevation / distance around the origin -> setCameraLookAt ------ */

static double g_az = 35.0;
static double g_el = 22.0;
static double g_dist = 8.0;

static void clamp_camera(void) {
    if (g_el > 80.0) g_el = 80.0;
    if (g_el < -10.0) g_el = -10.0;
    if (g_dist < 3.0) g_dist = 3.0;
    if (g_dist > 30.0) g_dist = 30.0;
    while (g_az >= 360.0) g_az -= 360.0;
    while (g_az < 0.0) g_az += 360.0;
}

/* Sends the camera to the extension and shows it; returns the invoke status (0 = ok). */
static int apply_camera(void) {
    KuiCExtensionValue args[6];
    KuiExtensionResult* result;
    char text[96];
    int status;
    clamp_camera();
    args[0] = v_num(g_dist * cos_rad(DEG(g_el)) * sin_rad(DEG(g_az)));
    args[1] = v_num(0.3 + g_dist * sin_rad(DEG(g_el)));
    args[2] = v_num(g_dist * cos_rad(DEG(g_el)) * cos_rad(DEG(g_az)));
    args[3] = v_num(0.0);
    args[4] = v_num(0.3);
    args[5] = v_num(0.0);
    result = call3d("setCameraLookAt", args, 6, 0);
    status = result_status(result);
    if (result) kui_extension_result_release(result);
    snprintf(text, sizeof text, "az=%.0f el=%.0f dist=%.1f", g_az, g_el, g_dist);
    kuex_set_text(g_doc, "cam-out", text);
    return status;
}

static void camera_step(double d_az, double d_el, double zoom) {
    g_az += d_az;
    g_el += d_el;
    g_dist *= zoom;
    apply_camera();
}

static void camera_reset(void) {
    g_az = 35.0;
    g_el = 22.0;
    g_dist = 8.0;
    apply_camera();
}

/* ---- Scene: ground, three meshes, two lights, PBR materials and the glTF tank --------------- */

typedef struct Scene {
    long long ground, box, sphere, cylinder, sun, lamp, tank;
    int transforms_ok, materials_ok, tank_transform_ok;
} Scene;

static Scene g_scene;
static int g_color;
static int g_spinning;
static double g_spin_angle;
static KuiTimerId g_spin_timer;
static int g_tank_on = 1;

static const double k_box_colors[4][3] = {
    {0.20, 0.55, 0.95}, {0.95, 0.65, 0.15}, {0.65, 0.30, 0.90}, {0.95, 0.95, 0.95}};

/* setEntityTransform(entity, position, rotation quaternion about Y, uniform scale) -> bool. */
static int place(long long entity, double x, double y, double z, double angle, double scale) {
    KuiCExtensionValue args[11];
    args[0] = v_int(entity);
    args[1] = v_num(x);
    args[2] = v_num(y);
    args[3] = v_num(z);
    args[4] = v_num(0.0);
    args[5] = v_num(sin_rad(angle * 0.5));
    args[6] = v_num(0.0);
    args[7] = v_num(cos_rad(angle * 0.5));
    args[8] = v_num(scale);
    args[9] = v_num(scale);
    args[10] = v_num(scale);
    return call_ok("setEntityTransform", args, 11);
}

/* createBox / createSphere / ...(r, g, b) -> entity, then addToScene and place it. */
static long long add_mesh(const char* method, double r, double g, double b,
                          double x, double y, double z, double scale) {
    KuiCExtensionValue args[3];
    KuiCExtensionValue entity_arg[1];
    long long entity;
    args[0] = v_num(r);
    args[1] = v_num(g);
    args[2] = v_num(b);
    entity = call_int(method, args, 3);
    if (!entity) return 0;
    entity_arg[0] = v_int(entity);
    if (!call_ok("addToScene", entity_arg, 1)) return 0;
    if (!place(entity, x, y, z, 0.0, scale)) g_scene.transforms_ok = 0;
    return entity;
}

/* setMaterialProperties(entity, r, g, b, metallic, roughness) -> bool. */
static int set_material(long long entity, double r, double g, double b,
                        double metallic, double roughness) {
    KuiCExtensionValue args[6];
    args[0] = v_int(entity);
    args[1] = v_num(r);
    args[2] = v_num(g);
    args[3] = v_num(b);
    args[4] = v_num(metallic);
    args[5] = v_num(roughness);
    return call_ok("setMaterialProperties", args, 6);
}

/* loadGltf(path) -> asset handle (also adds it to the scene); setAssetTransform places it. */
static void load_tank(void) {
    KuiCExtensionValue path[1];
    KuiCExtensionValue args[11];
    path[0] = v_str(g_tank_path);
    g_scene.tank = call_int("loadGltf", path, 1);
    g_scene.tank_transform_ok = 0;
    if (!g_scene.tank) return;
    args[0] = v_int(g_scene.tank);
    args[1] = v_num(0.0);
    args[2] = v_num(-0.75);
    args[3] = v_num(-2.6);
    args[4] = v_num(0.0);
    args[5] = v_num(0.0);
    args[6] = v_num(0.0);
    args[7] = v_num(1.0);
    args[8] = v_num(0.9);
    args[9] = v_num(0.9);
    args[10] = v_num(0.9);
    g_scene.tank_transform_ok = call_ok("setAssetTransform", args, 11);
}

static void recolor_box(void) {
    const double* c = k_box_colors[g_color];
    set_material(g_scene.box, c[0], c[1], c[2], 0.1, 0.45);
}

static void build_scene(void) {
    KuiCExtensionValue args[8];
    memset(&g_scene, 0, sizeof g_scene);
    g_scene.transforms_ok = 1;
    g_spin_angle = 0.0;

    args[0] = v_num(55.0);
    args[1] = v_num(0.1);
    args[2] = v_num(300.0);
    call_ok("setCameraProjection", args, 3);
    args[0] = v_num(0.04);
    args[1] = v_num(0.07);
    args[2] = v_num(0.14);
    call_ok("setSkyboxColor", args, 3);

    /* createDirectionalLight(r, g, b, intensity, dirX, dirY, dirZ) */
    args[0] = v_num(1.0);
    args[1] = v_num(0.95);
    args[2] = v_num(0.88);
    args[3] = v_num(110000.0);
    args[4] = v_num(-0.8);
    args[5] = v_num(-1.0);
    args[6] = v_num(-0.5);
    g_scene.sun = call_int("createDirectionalLight", args, 7);
    /* createPointLight(r, g, b, intensity, x, y, z, range) */
    args[0] = v_num(0.35);
    args[1] = v_num(0.6);
    args[2] = v_num(1.0);
    args[3] = v_num(60000.0);
    args[4] = v_num(-2.5);
    args[5] = v_num(3.5);
    args[6] = v_num(2.0);
    args[7] = v_num(12.0);
    g_scene.lamp = call_int("createPointLight", args, 8);

    g_scene.ground = add_mesh("createPlane", 0.20, 0.24, 0.30, 0.0, -0.75, 0.0, 9.0);
    g_scene.box = add_mesh("createBox", 0.2, 0.55, 0.95, 0.0, 0.0, 0.0, 1.4);
    g_scene.sphere = add_mesh("createSphere", 0.9, 0.25, 0.2, -2.4, 0.0, 0.0, 1.3);
    g_scene.cylinder = add_mesh("createCylinder", 0.25, 0.85, 0.45, 2.4, 0.0, 0.0, 1.1);

    g_scene.materials_ok = set_material(g_scene.sphere, 0.9, 0.25, 0.2, 0.9, 0.2) &&
                           set_material(g_scene.cylinder, 0.25, 0.85, 0.45, 0.0, 0.8);
    recolor_box();
    if (g_tank_on) load_tank();
    apply_camera();
}

/* A fresh <kui-3d-view id="view"> in #view-host (after a reload the old one is gone). */
static void create_view(void) {
    KuiElement* host = kuex_by_id(g_doc, "view-host");
    KuiElement* view = NULL;
    if (host && kui_document_create_element(g_doc, TAG_3D, 0, &view) == KUI_OK) {
        kui_element_set_attribute(view, "id", 0, "view", 0);
        kui_element_append_child(host, view);
        kui_element_release(view);
    }
    if (host) kui_element_release(host);
}

/* Removes the 3D element before its extension goes away, so the element never outlives the
 * code that renders it. */
static void remove_view(void) {
    KuiElement* view = kuex_by_id(g_doc, "view");
    if (!view) return;
    kui_element_remove(view);
    kui_element_release(view);
}

/* ---- Mouse orbit: listeners on #view-host (events bubble up from the 3D element) ------------
 * Left drag orbits, right drag (vertical) zooms, every wheel event zooms in one step: the C ABI
 * has no accessor for a DOM wheel event's delta, so the direction is not available. */

static int g_drag_button;
static float g_last_x, g_last_y;

static void on_view_down(KuiEvent* event, void* user_data) {
    int button = 0;
    (void)user_data;
    if (kui_event_mouse_get_client_xy(event, &g_last_x, &g_last_y) != KUI_OK) return;
    kui_event_mouse_get_button(event, &button);
    g_drag_button = button;
}

static void on_view_move(KuiEvent* event, void* user_data) {
    float x, y;
    (void)user_data;
    if (!g_drag_button || kui_event_mouse_get_client_xy(event, &x, &y) != KUI_OK) return;
    if (g_drag_button == KUI_C_MOUSE_BUTTON_LEFT) {
        camera_step(-(x - g_last_x) * 0.5, (y - g_last_y) * 0.3, 1.0);
    } else if (g_drag_button == KUI_C_MOUSE_BUTTON_RIGHT) {
        camera_step(0.0, 0.0, 1.0 + (y - g_last_y) * 0.01);
    }
    g_last_x = x;
    g_last_y = y;
}

static void on_view_up(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    g_drag_button = 0;
}

static void on_view_wheel(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    camera_step(0.0, 0.0, 0.9);
}

/* ---- Camera and scene buttons ---------------------------------------------------------------- */

static void on_cam_left(KuiEvent* e, void* u) { (void)e; (void)u; camera_step(-15.0, 0.0, 1.0); }
static void on_cam_right(KuiEvent* e, void* u) { (void)e; (void)u; camera_step(15.0, 0.0, 1.0); }
static void on_cam_up(KuiEvent* e, void* u) { (void)e; (void)u; camera_step(0.0, 10.0, 1.0); }
static void on_cam_down(KuiEvent* e, void* u) { (void)e; (void)u; camera_step(0.0, -10.0, 1.0); }
static void on_cam_in(KuiEvent* e, void* u) { (void)e; (void)u; camera_step(0.0, 0.0, 0.8); }
static void on_cam_out(KuiEvent* e, void* u) { (void)e; (void)u; camera_step(0.0, 0.0, 1.25); }
static void on_cam_reset(KuiEvent* e, void* u) { (void)e; (void)u; camera_reset(); }

static void spin_tick(void* user_data) {
    (void)user_data;
    g_spin_angle += 0.06;
    place(g_scene.box, 0.0, 0.0, 0.0, g_spin_angle, 1.4);
    place(g_scene.cylinder, 2.4, 0.0, 0.0, -g_spin_angle, 1.1);
}

static void show_scene_state(void) {
    char text[160];
    snprintf(text, sizeof text, "color=%d spin=%s tank=%s entities=%lld,%lld,%lld,%lld",
             g_color, g_spinning ? "on" : "off", g_scene.tank ? "on" : "off", g_scene.ground,
             g_scene.box, g_scene.sphere, g_scene.cylinder);
    kuex_set_text(g_doc, "scene-out", text);
}

static void on_scene_color(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    g_color = (g_color + 1) % 4;
    recolor_box();
    show_scene_state();
}

static void on_scene_spin(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    if (g_spinning) {
        kui_platform_clear_timer(g_spin_timer);
        g_spinning = 0;
    } else if (kui_platform_set_interval(spin_tick, NULL, 33, &g_spin_timer) == KUI_OK) {
        g_spinning = 1;
    }
    show_scene_state();
}

static void on_scene_tank(KuiEvent* e, void* u) {
    KuiCExtensionValue handle[1];
    (void)e;
    (void)u;
    if (g_scene.tank) {
        handle[0] = v_int(g_scene.tank);
        call_ok("destroyAsset", handle, 1);
        g_scene.tank = 0;
        g_tank_on = 0;
    } else {
        g_tank_on = 1;
        load_tank();
    }
    show_scene_state();
}

/* ---- Extension manager buttons ---------------------------------------------------------------- */

static void show_ext_state(const char* prefix) {
    size_t count = 0;
    int watching = 0, loaded = 0;
    char text[200];
    kui_document_get_extension_state(g_doc, &count, &watching);
    kui_document_extension_is_loaded(g_doc, TAG_3D, 0, &loaded);
    snprintf(text, sizeof text, "%sloaded=%d extensions=%u watching=%d", prefix, loaded,
             (unsigned)count, watching);
    kuex_set_text(g_doc, "ext-out", text);
}

static void show_perf(const char* prefix) {
    KuiCExtensionPerfStats stats;
    char text[200];
    memset(&stats, 0, sizeof stats);
    kui_document_extension_get_perf_stats(g_doc, TAG_3D, 0, &stats);
    snprintf(text, sizeof text, "%sticks=%llu paints=%llu max_tick=%.2fms max_paint=%.2fms",
             prefix, (unsigned long long)stats.tick_count, (unsigned long long)stats.paint_count,
             stats.max_tick_ms, stats.max_paint_ms);
    kuex_set_text(g_doc, "ext-out", text);
}

static void on_ext_state(KuiEvent* e, void* u) { (void)e; (void)u; show_ext_state(""); }
static void on_ext_perf(KuiEvent* e, void* u) { (void)e; (void)u; show_perf(""); }

static void on_ext_reset(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_document_extension_reset_perf_stats(g_doc);
    show_perf("reset: ");
}

/* Unload -> reload from the DLL path -> fresh element -> rebuilt scene. */
static int reload_3d(void) {
    int reloaded = 0;
    if (g_spinning) {
        kui_platform_clear_timer(g_spin_timer);
        g_spinning = 0;
    }
    remove_view();
    kui_document_unload_extension(g_doc, TAG_3D, 0);
    kui_document_reload_extension(g_doc, g_ext_path, 0, &reloaded);
    create_view();
    build_scene();
    return reloaded;
}

static void on_ext_reload(KuiEvent* e, void* u) {
    char prefix[64];
    (void)e;
    (void)u;
    snprintf(prefix, sizeof prefix, "reloaded=%d box=%s | ", reload_3d(),
             g_scene.box ? "rebuilt" : "missing");
    show_ext_state(prefix);
}

static void on_ext_watch(KuiEvent* e, void* u) {
    int watching = 0;
    (void)e;
    (void)u;
    kui_document_get_extension_state(g_doc, NULL, &watching);
    kui_document_watch_extensions(g_doc, watching ? NULL : g_exe_dir, 0);
    show_ext_state("");
}

/* ---- Invoke result buttons ---------------------------------------------------------------------- */

static void show_invoke(const char* method, size_t arg_count, size_t max_results) {
    KuiCExtensionValue none[1];
    KuiExtensionResult* result;
    char text[300];
    none[0] = v_int(0);
    result = call3d(method, arg_count ? none : NULL, arg_count, max_results);
    if (!result) {
        kuex_set_text(g_doc, "res-out", "no result (no 3D view)");
        return;
    }
    describe_result(result, text, sizeof text);
    kui_extension_result_release(result);
    kuex_set_text(g_doc, "res-out", text);
}

static void on_res_ok(KuiEvent* e, void* u) { (void)e; (void)u; show_invoke("getRenderStats", 0, 4); }
static void on_res_unknown(KuiEvent* e, void* u) { (void)e; (void)u; show_invoke("noSuchMethod", 0, 1); }
static void on_res_args(KuiEvent* e, void* u) { (void)e; (void)u; show_invoke("addToScene", 0, 1); }

/* ---- Self-check: the scene built through extension calls ------------------------------------- */

static void check_scene(const char* prefix) {
    char name[64];
    snprintf(name, sizeof name, "%s.lights", prefix);
    check(name, g_scene.sun != 0 && g_scene.lamp != 0, "sun %lld, point light %lld",
          g_scene.sun, g_scene.lamp);
    snprintf(name, sizeof name, "%s.meshes", prefix);
    check(name, g_scene.ground && g_scene.box && g_scene.sphere && g_scene.cylinder &&
                    g_scene.box != g_scene.sphere && g_scene.sphere != g_scene.cylinder &&
                    g_scene.ground != g_scene.box,
          "entities %lld %lld %lld %lld", g_scene.ground, g_scene.box, g_scene.sphere,
          g_scene.cylinder);
    snprintf(name, sizeof name, "%s.transforms", prefix);
    check(name, g_scene.transforms_ok, "setEntityTransform returned false");
    snprintf(name, sizeof name, "%s.materials", prefix);
    check(name, g_scene.materials_ok, "setMaterialProperties returned false");
    snprintf(name, sizeof name, "%s.gltf", prefix);
    check(name, g_scene.tank != 0 && g_scene.tank_transform_ok,
          "loadGltf(%s) -> %lld, setAssetTransform %d", g_tank_path, g_scene.tank,
          g_scene.tank_transform_ok);
}

/* ---- Self-check: invoke result objects (status, count, values, error) ------------------------ */

static void check_results(void) {
    KuiCExtensionValue value;
    KuiExtensionResult* result;
    char error[200];
    size_t index;
    int all_int = 1;

    result = call3d("getRenderStats", NULL, 0, 4);
    for (index = 0; index < result_count(result); ++index) {
        memset(&value, 0, sizeof value);
        if (kui_extension_result_get_value(result, index, &value) != KUI_OK ||
            value.type != KUI_C_EXT_INT) {
            all_int = 0;
        }
    }
    check("res.get_count", result_status(result) == 0 && result_count(result) == 4 && all_int,
          "getRenderStats: status %d count %u", result_status(result),
          (unsigned)result_count(result));
    result_error(result, error, sizeof error);
    check("res.get_error-empty", result && error[0] == '\0', "error on success '%s'", error);
    if (result) kui_extension_result_release(result);

    result = call3d("noSuchMethod", NULL, 0, 1);
    result_error(result, error, sizeof error);
    check("res.status-unknown-method",
          result_status(result) == 3 && result_count(result) == 0 && strstr(error, "noSuchMethod"),
          "status %d count %u error '%s'", result_status(result), (unsigned)result_count(result),
          error);
    if (result) kui_extension_result_release(result);

    result = call3d("addToScene", NULL, 0, 1);
    result_error(result, error, sizeof error);
    check("res.status-arg-count", result_status(result) == 4 && error[0] != '\0',
          "status %d error '%s'", result_status(result), error);
    if (result) kui_extension_result_release(result);
}

/* ---- Self-check: the orbit camera, through the buttons' function and real mouse events ------- */

static int approx(double a, double b) { return a - b < 0.01 && b - a < 0.01; }

static void check_camera(void) {
    KuiElement* host = kuex_by_id(g_doc, "view-host");
    KuiRect rect;
    float cx = 0.0f, cy = 0.0f;
    double before;

    check("cam.set_camera_look_at", apply_camera() == 0, "setCameraLookAt failed");
    camera_step(15.0, 0.0, 1.0);
    check("cam.step", approx(g_az, 50.0), "azimuth %.2f after +15", g_az);

    memset(&rect, 0, sizeof rect);
    if (host) kui_element_get_bounding_rect(host, &rect);
    cx = rect.x + rect.width * 0.5f;
    cy = rect.y + rect.height * 0.5f;
    if (host) kui_element_release(host);

    /* Left drag 40 px to the right: the mousedown / mousemove / mouseup listeners turn it into
     * -20 degrees of azimuth. */
    before = g_az;
    kui_devtools_dispatch_mouse_event(g_doc, KUI_C_MOUSE_PRESSED, cx, cy, KUI_C_MOUSE_BUTTON_LEFT,
                                      0, 1, 0.0f, 0.0f);
    kui_devtools_dispatch_mouse_event(g_doc, KUI_C_MOUSE_MOVED, cx + 40.0f, cy,
                                      KUI_C_MOUSE_BUTTON_LEFT, 0, 0, 0.0f, 0.0f);
    kui_devtools_dispatch_mouse_event(g_doc, KUI_C_MOUSE_RELEASED, cx + 40.0f, cy,
                                      KUI_C_MOUSE_BUTTON_LEFT, 0, 1, 0.0f, 0.0f);
    check("cam.mouse-drag-orbit", approx(g_az, before - 20.0) && g_drag_button == 0,
          "azimuth %.2f -> %.2f, drag button %d", before, g_az, g_drag_button);

    /* Right drag 20 px down zooms out by 20 %. */
    before = g_dist;
    kui_devtools_dispatch_mouse_event(g_doc, KUI_C_MOUSE_PRESSED, cx, cy,
                                      KUI_C_MOUSE_BUTTON_RIGHT, 0, 1, 0.0f, 0.0f);
    kui_devtools_dispatch_mouse_event(g_doc, KUI_C_MOUSE_MOVED, cx, cy + 20.0f,
                                      KUI_C_MOUSE_BUTTON_RIGHT, 0, 0, 0.0f, 0.0f);
    kui_devtools_dispatch_mouse_event(g_doc, KUI_C_MOUSE_RELEASED, cx, cy + 20.0f,
                                      KUI_C_MOUSE_BUTTON_RIGHT, 0, 1, 0.0f, 0.0f);
    check("cam.mouse-drag-zoom", approx(g_dist, before * 1.2), "distance %.2f -> %.2f", before,
          g_dist);

    before = g_dist;
    kui_devtools_dispatch_mouse_event(g_doc, KUI_C_MOUSE_WHEEL, cx, cy, KUI_C_MOUSE_BUTTON_NONE, 0,
                                      0, 0.0f, -120.0f);
    check("cam.mouse-wheel", approx(g_dist, before * 0.9), "distance %.2f -> %.2f", before, g_dist);
    camera_reset();
}

/* ---- Self-check: extension manager state, perf stats and directory watching ------------------- */

static size_t g_initial_count;

static size_t ext_count(void) {
    size_t count = 0;
    kui_document_get_extension_state(g_doc, &count, NULL);
    return count;
}

static int ext_loaded(void) {
    int loaded = -1;
    kui_document_extension_is_loaded(g_doc, TAG_3D, 0, &loaded);
    return loaded;
}

static void check_manager(void) {
    KuiCExtensionPerfStats stats;
    KuiStatus status;
    int watching = -1;

    check("ext.is_loaded", ext_loaded() == 1, "kui-3d-view loaded=%d", ext_loaded());
    status = kui_document_get_extension_state(g_doc, &g_initial_count, &watching);
    check("ext.get_extension_state", status == KUI_OK && g_initial_count >= 1 && watching == 0,
          "status %d count %u watching %d", status, (unsigned)g_initial_count, watching);

    status = kui_document_watch_extensions(g_doc, g_exe_dir, 0);
    kui_document_get_extension_state(g_doc, NULL, &watching);
    check("ext.watch_extensions-on", status == KUI_OK && watching == 1,
          "status %d watching %d", status, watching);
    status = kui_document_watch_extensions(g_doc, NULL, 0);
    kui_document_get_extension_state(g_doc, NULL, &watching);
    check("ext.watch_extensions-off", status == KUI_OK && watching == 0,
          "status %d watching %d", status, watching);

    memset(&stats, 0xff, sizeof stats);
    status = kui_document_extension_get_perf_stats(g_doc, TAG_3D, 0, &stats);
    check("perf.get_perf_stats", status == KUI_OK && stats.tick_count != (uint64_t)-1,
          "status %d", status);
}

/* Unload -> confirm -> reload -> load, before the page has any 3D element: the element is
 * created afterwards, on the extension that is loaded at the end. (The "Unload + reload 3D" button
 * does the same with a live view; the example's pixel check covers that second view.) */
static void check_lifecycle(void) {
    KuiStatus status;
    int flag = -1;
    char missing[1100];

    status = kui_document_unload_extension(g_doc, TAG_3D, 0);
    check("ext.unload_extension", status == KUI_OK && ext_loaded() == 0 &&
                                      ext_count() == g_initial_count - 1,
          "status %d loaded %d count %u", status, ext_loaded(), (unsigned)ext_count());
    status = kui_document_unload_extension(g_doc, TAG_3D, 0);
    check("ext.unload_extension-again", status == KUI_ERR_NOT_FOUND, "status %d", status);

    status = kui_document_reload_extension(g_doc, g_ext_path, 0, &flag);
    check("ext.reload_extension", status == KUI_OK && flag == 1 && ext_loaded() == 1 &&
                                      ext_count() == g_initial_count,
          "status %d reloaded %d loaded %d count %u", status, flag, ext_loaded(),
          (unsigned)ext_count());

    snprintf(missing, sizeof missing, "%s/no_such_extension_%s", g_exe_dir, EXT_3D_FILE);
    flag = -1;
    status = kui_document_load_extension(g_doc, missing, 0, &flag);
    check("ext.load_extension-missing", status == KUI_OK && flag == 0 &&
                                            ext_count() == g_initial_count,
          "status %d loaded %d count %u", status, flag, (unsigned)ext_count());

    /* unload_all removes the custom element definitions of every extension it unloads (like
     * unload_extension does), so the DLLs can be loaded again below. */
    status = kui_document_unload_all_extensions(g_doc);
    check("ext.unload_all_extensions", status == KUI_OK && ext_count() == 0 && ext_loaded() == 0,
          "status %d count %u loaded %d", status, (unsigned)ext_count(), ext_loaded());
    flag = -1;
    kui_document_custom_element_is_defined(g_doc, "kui-editor", 0, &flag);
    check("ext.unload_all-undefines-elements", flag == 0,
          "<kui-editor> still defined=%d", flag);

    flag = -1;
    status = kui_document_load_extension(g_doc, g_ext_path, 0, &flag);
    check("ext.load_extension", status == KUI_OK && flag == 1 && ext_count() == 1 &&
                                    ext_loaded() == 1,
          "status %d loaded %d count %u", status, flag, (unsigned)ext_count());
}

/* After a few frames: what the perf counters saw, then reset them; publish the verdict. */
static void check_perf_and_finish(void* user_data) {
    KuiCExtensionPerfStats stats;
    KuiStatus status;
    (void)user_data;

    memset(&stats, 0, sizeof stats);
    kui_document_extension_get_perf_stats(g_doc, TAG_3D, 0, &stats);
    known("perf.paints-recorded", stats.paint_count > 0 || stats.tick_count > 0,
          "ExtensionManager never records tick / paint timings (recordTickPerf / recordPaintPerf "
          "have no caller)",
          "ticks %llu paints %llu after rendering", (unsigned long long)stats.tick_count,
          (unsigned long long)stats.paint_count);
    status = kui_document_extension_reset_perf_stats(g_doc);
    memset(&stats, 0xff, sizeof stats);
    kui_document_extension_get_perf_stats(g_doc, TAG_3D, 0, &stats);
    check("perf.reset_perf_stats", status == KUI_OK && stats.tick_count == 0 &&
                                       stats.paint_count == 0 && stats.total_paint_ms == 0.0,
          "status %d ticks %llu paints %llu", status, (unsigned long long)stats.tick_count,
          (unsigned long long)stats.paint_count);
    show_ext_state("self-check done | ");
    finish();
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    KuiTimerId timer = 0;
    int failed = 0;
    g_doc = host->document;
    find_paths();

    failed |= kuex_on(g_doc, "view-host", "mousedown", on_view_down, NULL);
    failed |= kuex_on(g_doc, "view-host", "mousemove", on_view_move, NULL);
    failed |= kuex_on(g_doc, "view-host", "mouseup", on_view_up, NULL);
    failed |= kuex_on(g_doc, "view-host", "mouseleave", on_view_up, NULL);
    failed |= kuex_on(g_doc, "view-host", "wheel", on_view_wheel, NULL);

    check_manager();
    check_lifecycle();
    create_view();
    build_scene();
    check_scene("scene");
    check_results();
    check_camera();
    show_scene_state();
    if (kui_platform_set_timeout(check_perf_and_finish, NULL, 400, &timer) != KUI_OK) {
        check("perf.timer", 0, "kui_platform_set_timeout failed");
        finish();
    }

    failed |= kuex_on(g_doc, "btn-cam-left", "click", on_cam_left, NULL);
    failed |= kuex_on(g_doc, "btn-cam-right", "click", on_cam_right, NULL);
    failed |= kuex_on(g_doc, "btn-cam-up", "click", on_cam_up, NULL);
    failed |= kuex_on(g_doc, "btn-cam-down", "click", on_cam_down, NULL);
    failed |= kuex_on(g_doc, "btn-cam-in", "click", on_cam_in, NULL);
    failed |= kuex_on(g_doc, "btn-cam-out", "click", on_cam_out, NULL);
    failed |= kuex_on(g_doc, "btn-cam-reset", "click", on_cam_reset, NULL);
    failed |= kuex_on(g_doc, "btn-scene-color", "click", on_scene_color, NULL);
    failed |= kuex_on(g_doc, "btn-scene-spin", "click", on_scene_spin, NULL);
    failed |= kuex_on(g_doc, "btn-scene-tank", "click", on_scene_tank, NULL);
    failed |= kuex_on(g_doc, "btn-ext-state", "click", on_ext_state, NULL);
    failed |= kuex_on(g_doc, "btn-ext-perf", "click", on_ext_perf, NULL);
    failed |= kuex_on(g_doc, "btn-ext-reset", "click", on_ext_reset, NULL);
    failed |= kuex_on(g_doc, "btn-ext-reload", "click", on_ext_reload, NULL);
    failed |= kuex_on(g_doc, "btn-ext-watch", "click", on_ext_watch, NULL);
    failed |= kuex_on(g_doc, "btn-res-ok", "click", on_res_ok, NULL);
    failed |= kuex_on(g_doc, "btn-res-unknown", "click", on_res_unknown, NULL);
    failed |= kuex_on(g_doc, "btn-res-args", "click", on_res_args, NULL);
    return failed;
}
