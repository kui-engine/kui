// =============================================================================
// kui_c_platform.cpp — process lifecycle, event loop, timers, windows
//
// Note what is absent: any KuiApplication. KUI has no Application class; the
// corresponding C++ surface is the all-static kui::Platform, so these functions
// take no handle. See the header for the call-order contract.
// =============================================================================
#include "kui_c_internal.h"

#include <kui/dom/document.h>
#include <kui/platform/platform.h>
#include <kui/devtools/devtools_server.h>
#include <kui/platform/window.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <mutex>

using kui_c::fromHandle;
using kui_c::str;
using kui_c::toHandle;
using kui_c::writeString;

namespace {

inline kui::Window* win(KuiWindow* h) {
    return fromHandle<kui::Window>(h);
}

// Every field treats 0 as "keep the default", so a zero-initialized KuiWindowProps yields an
// ordinary window. That is what lets a C caller memset the struct and set only what matters.
void applyBaseProps(kui::WindowConfig& cfg, const KuiWindowProps* props) {
    if (!props) return;
    if (props->title) cfg.title = str(props->title, props->title_len);
    if (props->width  > 0) cfg.width  = static_cast<kui::u32>(props->width);
    if (props->height > 0) cfg.height = static_cast<kui::u32>(props->height);
    if (props->min_width  > 0) cfg.minWidth  = static_cast<kui::u32>(props->min_width);
    if (props->min_height > 0) cfg.minHeight = static_cast<kui::u32>(props->min_height);
    if (props->max_width  > 0) cfg.maxWidth  = static_cast<kui::u32>(props->max_width);
    if (props->max_height > 0) cfg.maxHeight = static_cast<kui::u32>(props->max_height);
    // Negatively phrased because WindowConfig defaults both to true and Window exposes no setter
    // for either -- see the header. This keeps a zero-initialized struct meaning "ordinary window"
    // while still letting both be disabled.
    if (props->no_resize)      cfg.resizable = false;
    if (props->no_decorations) cfg.decorated = false;
    if (props->transparent)      cfg.transparent = true;
    if (props->always_on_top)    cfg.alwaysOnTop = true;
    if (props->maximized)        cfg.maximized   = true;
    if (props->centered)         cfg.centered    = true;
    if (props->frameless)        cfg.frameless   = true;
    if (props->frameless_shadow) cfg.shadow      = true;
    if (props->opacity > 0.0f)   cfg.opacity = props->opacity;
}

// Timer and idle callbacks use the same trampoline shape as event listeners:
// capture two POD pointers only. kui::Callback relocates its inline storage with
// memcpy, so a capture with non-trivial move semantics would be unsound, and an
// owning capture would raise a lifetime question the C caller cannot answer.
// user_data therefore stays the caller's responsibility, as documented.
inline kui::Callback<void()> makeTaskCallback(KuiTaskFn fn, void* user_data) {
    return [fn, user_data]() {
        try {
            fn(user_data);
        } catch (...) {
            // A C function pointer must not have an exception unwind through it,
            // and there is no channel to report one from inside the event loop.
        }
    };
}

// Whether kui::Platform is between init and shutdown, as seen by this layer.
// kui::Platform::postToMainThread itself accepts posts at any time: before init
// they wait for init, after shutdown they sit in a static queue until process
// exit and are destroyed during static destruction -- where a discard callback
// into Go or Rust must not run. Posts therefore go through this flag, and
// shutdown clears it under the same mutex before tearing the platform down, so
// every accepted post is either executed or dropped by that shutdown.
std::mutex g_postMutex;
bool g_platformRunning = false;

}  // namespace

bool kui_c::postToMainThread(const std::shared_ptr<MainThreadTask>& task) {
    std::lock_guard<std::mutex> lock(g_postMutex);
    if (!g_platformRunning) {
        task->settled = true;
        return false;
    }
    try {
        kui::Platform::postToMainThread([task]() {
            task->settled = true;
            try {
                task->run();
            } catch (...) {
            }
        });
    } catch (...) {
        task->settled = true;
        return false;
    }
    return true;
}

bool kui_c::platformRunning() {
    std::lock_guard<std::mutex> lock(g_postMutex);
    return g_platformRunning;
}

extern "C" {

// --- Platform ----------------------------------------------------------------

KuiStatus kui_platform_init(void) {
    KUI_C_BEGIN
        if (!kui::Platform::init()) return KUI_ERR_INTERNAL;
        std::lock_guard<std::mutex> lock(g_postMutex);
        g_platformRunning = true;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_shutdown(void) {
    {
        // Refuse new posts first; a post that already holds the mutex finishes
        // queueing and is then dropped (discarded) by Platform::shutdown below.
        // The lock is released before shutdown so a discard callback can never
        // deadlock against it.
        std::lock_guard<std::mutex> lock(g_postMutex);
        g_platformRunning = false;
    }
    KUI_C_BEGIN
        kui::Platform::shutdown();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_run(int* out_exit_code) {
    KUI_C_BEGIN
        // Blocks until quit(). out_exit_code is optional so a caller that
        // ignores the code does not need a dummy variable.
        const int code = kui::Platform::run();
        if (out_exit_code) *out_exit_code = code;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_quit(int exit_code) {
    KUI_C_BEGIN
        kui::Platform::quit(exit_code);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_poll_events(void) {
    KUI_C_BEGIN
        kui::Platform::pollEvents();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_wait_events(double timeout_seconds) {
    KUI_C_BEGIN
        kui::Platform::waitEvents(static_cast<kui::f64>(timeout_seconds));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_set_timeout(KuiTaskFn fn, void* user_data,
                                   uint32_t delay_ms, KuiTimerId* out_id) {
    if (!fn) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const kui::u32 id = kui::Platform::setTimeout(makeTaskCallback(fn, user_data),
                                                      static_cast<kui::u32>(delay_ms));
        if (out_id) *out_id = static_cast<KuiTimerId>(id);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_set_interval(KuiTaskFn fn, void* user_data,
                                    uint32_t interval_ms, KuiTimerId* out_id) {
    if (!fn) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const kui::u32 id = kui::Platform::setInterval(makeTaskCallback(fn, user_data),
                                                       static_cast<kui::u32>(interval_ms));
        if (out_id) *out_id = static_cast<KuiTimerId>(id);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_clear_timer(KuiTimerId id) {
    KUI_C_BEGIN
        kui::Platform::clearTimer(static_cast<kui::u32>(id));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_request_idle(KuiTaskFn fn, void* user_data) {
    if (!fn) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::Platform::requestIdle(makeTaskCallback(fn, user_data));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_post_to_main_thread(KuiTaskFn fn, KuiTaskFn discard_fn,
                                           void* user_data) {
    if (!fn) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto task = std::make_shared<kui_c::MainThreadTask>();
        task->run = [fn, user_data]() { fn(user_data); };
        if (discard_fn) task->discard = [discard_fn, user_data]() { discard_fn(user_data); };
        return kui_c::postToMainThread(task) ? KUI_OK : KUI_ERR_NOT_SUPPORTED;
    KUI_C_END
}

KuiStatus kui_platform_set_f12_devtools_enabled(int enabled) {
    KUI_C_BEGIN
        kui::Platform::setF12DevToolsEnabled(enabled != 0);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_get_primary_monitor(KuiCMonitorInfo* out_monitor) {
    if (!out_monitor) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const kui::Platform::MonitorInfo monitor = kui::Platform::primaryMonitor();
        out_monitor->bounds_x = monitor.bounds.x;
        out_monitor->bounds_y = monitor.bounds.y;
        out_monitor->bounds_width = monitor.bounds.width;
        out_monitor->bounds_height = monitor.bounds.height;
        out_monitor->work_x = monitor.workArea.x;
        out_monitor->work_y = monitor.workArea.y;
        out_monitor->work_width = monitor.workArea.width;
        out_monitor->work_height = monitor.workArea.height;
        out_monitor->scale = monitor.scale;
        out_monitor->is_primary = monitor.isPrimary ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

// --- Window ------------------------------------------------------------------

KuiStatus kui_window_create(const KuiWindowProps* props, KuiWindow** out) {
    if (!out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        kui::WindowConfig cfg;    // defaults from the C++ side
        applyBaseProps(cfg, props);

        kui::Unique<kui::Window> w = kui::Window::create(cfg);
        if (!w) return KUI_ERR_INTERNAL;
        // Uniquely owned: hand the raw pointer to the caller, who must release it
        // exactly once. release() detaches without destroying.
        *out = toHandle<KuiWindow>(w.release());
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_release(KuiWindow* w) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        // Destroys the window -- this is not a reference-count decrement.
        kui::Window* window = win(w);
        delete window;
        // Its menu / tray callbacks died with it; drop the C-side copies.
        kui_c::forgetWindow(window);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_show(KuiWindow* w) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->show();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_hide(KuiWindow* w) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->hide();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_is_visible(KuiWindow* w, int* out_visible) {
    if (!w || !out_visible) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_visible = win(w)->isVisible() ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_get_document(KuiWindow* w, KuiDocument** out) {
    if (!w || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    KUI_C_BEGIN
        kui::Document* doc = win(w)->document();
        if (!doc) return KUI_ERR_NOT_FOUND;
        // Take a reference, as every handle-yielding function does. This is what
        // keeps the document usable after the window is released.
        doc->ref();
        *out = toHandle<KuiDocument>(doc);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_get_title(KuiWindow* w, char* buf, size_t buf_capacity, size_t* out_len) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const kui::String title = win(w)->title();
        return writeString(title, buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_window_set_title(KuiWindow* w, const char* title, size_t len) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->setTitle(str(title, len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_get_size(KuiWindow* w, int* out_width, int* out_height) {
    if (!w || !out_width || !out_height) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_width = static_cast<int>(win(w)->width());
        *out_height = static_cast<int>(win(w)->height());
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_resize(KuiWindow* w, int width, int height) {
    if (!w) return KUI_ERR_INVALID_ARG;
    if (width < 0 || height < 0) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->resize(static_cast<kui::u32>(width), static_cast<kui::u32>(height));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_get_position(KuiWindow* w, int* out_x, int* out_y) {
    if (!w || !out_x || !out_y) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const kui::Point position = win(w)->position();
        *out_x = static_cast<int>(position.x);
        *out_y = static_cast<int>(position.y);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_set_position(KuiWindow* w, int x, int y) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->setPosition(static_cast<kui::i32>(x), static_cast<kui::i32>(y));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_minimize(KuiWindow* w) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->minimize();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_maximize(KuiWindow* w) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->maximize();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_restore(KuiWindow* w) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->restore();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_set_always_on_top(KuiWindow* w, int on_top) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->setAlwaysOnTop(on_top != 0);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_set_opacity(KuiWindow* w, float opacity) {
    if (!w) return KUI_ERR_INVALID_ARG;
    if (opacity < 0.0f || opacity > 1.0f) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->setOpacity(opacity);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_get_device_pixel_ratio(KuiWindow* w, float* out_ratio) {
    if (!w || !out_ratio) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_ratio = win(w)->devicePixelRatio();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_is_minimized(KuiWindow* w, int* out_minimized) {
    if (!w || !out_minimized) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_minimized = win(w)->isMinimized() ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_is_maximized(KuiWindow* w, int* out_maximized) {
    if (!w || !out_maximized) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_maximized = win(w)->isMaximized() ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_close(KuiWindow* w) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->close();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_should_close(KuiWindow* w, int* out_should_close) {
    if (!w || !out_should_close) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_should_close = win(w)->shouldClose() ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_on_resize(KuiWindow* w, KuiWindowResizeFn fn, void* user_data) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        if (!fn) {
            win(w)->onResize(nullptr);
        } else {
            win(w)->onResize([fn, user_data](kui::u32 width, kui::u32 height) {
                fn(static_cast<uint32_t>(width), static_cast<uint32_t>(height), user_data);
            });
        }
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_start_devtools(KuiWindow* w, uint16_t port, int* out_started) {
    if (!w || !out_started) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_started = win(w)->startDevTools(static_cast<kui::u16>(port)) ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_stop_devtools(KuiWindow* w) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        if (kui::DevToolsServer* server = win(w)->devToolsServer()) server->stop();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_get_devtools_state(KuiWindow* w, int* out_running, uint16_t* out_port,
                                        uint32_t* out_clients) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::DevToolsServer* server = win(w)->devToolsServer();
        const bool running = server && server->isRunning();
        if (out_running) *out_running = running ? 1 : 0;
        if (out_port) *out_port = running ? server->port() : 0;
        if (out_clients) *out_clients = running ? server->clientCount() : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_get_devtools_url(KuiWindow* w, char* buf, size_t buf_capacity,
                                      size_t* out_len) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const kui::String url = win(w)->devToolsUrl();
        return writeString(url, buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_window_create_ex(const KuiWindowPropsEx* props, KuiWindow** out) {
    if (!out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    if (!props || props->struct_size < sizeof(KuiWindowPropsEx)) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::WindowConfig cfg;
        applyBaseProps(cfg, &props->base);
        if (props->sizeable) cfg.sizeable = true;
        if (props->hidden)   cfg.visible  = false;
        if (props->has_frame_radius) cfg.frameRadius = props->frame_radius;
        kui::Unique<kui::Window> w = kui::Window::create(cfg);
        if (!w) return KUI_ERR_INTERNAL;
        *out = toHandle<KuiWindow>(w.release());
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_set_enabled(KuiWindow* w, int enabled) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
#ifdef _WIN32
        HWND hwnd = static_cast<HWND>(win(w)->nativeHandle());
        if (!hwnd) return KUI_ERR_NOT_FOUND;
        ::EnableWindow(hwnd, enabled ? TRUE : FALSE);
        return KUI_OK;
#else
        (void)enabled;
        return KUI_ERR_NOT_SUPPORTED;
#endif
    KUI_C_END
}

KuiStatus kui_window_is_enabled(KuiWindow* w, int* out_enabled) {
    if (!w || !out_enabled) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
#ifdef _WIN32
        HWND hwnd = static_cast<HWND>(win(w)->nativeHandle());
        if (!hwnd) return KUI_ERR_NOT_FOUND;
        *out_enabled = ::IsWindowEnabled(hwnd) ? 1 : 0;
        return KUI_OK;
#else
        return KUI_ERR_NOT_SUPPORTED;
#endif
    KUI_C_END
}

KuiStatus kui_window_process_frame(KuiWindow* w) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->processFrame();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_set_devtools_token_auth(KuiWindow* w, int enabled) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::DevToolsServer* server = win(w)->devToolsServer();
        if (!server) return KUI_ERR_NOT_FOUND;
        server->setTokenAuth(enabled != 0);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_disable_ime(void) {
    KUI_C_BEGIN
#ifdef _WIN32
        // imm32.dll is loaded by user32 in every GUI process; resolve the function at run time so
        // kui_c.dll needs no extra import library.
        HMODULE imm = ::LoadLibraryW(L"imm32.dll");
        if (!imm) return KUI_ERR_NOT_SUPPORTED;
        using DisableImeFn = BOOL(WINAPI*)(DWORD);
        auto disable = reinterpret_cast<DisableImeFn>(::GetProcAddress(imm, "ImmDisableIME"));
        if (!disable) return KUI_ERR_NOT_SUPPORTED;
        return disable(static_cast<DWORD>(-1)) ? KUI_OK : KUI_ERR_NOT_SUPPORTED;
#else
        return KUI_ERR_NOT_SUPPORTED;
#endif
    KUI_C_END
}

KuiStatus kui_window_get_native_handle(KuiWindow* w, void** out_handle) {
    if (!w || !out_handle) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_handle = win(w)->nativeHandle();
        return KUI_OK;
    KUI_C_END
}

}  // extern "C"
