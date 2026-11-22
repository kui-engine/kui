// =============================================================================
// kui_c_full.cpp — additive Windows Full Profile bridge
// =============================================================================
#include "kui_c_internal.h"

#include <kui/animation/lottie_player.h>
#include <kui/component/custom_element.h>
#include <kui/devtools/devtools_server.h>
#include <kui/dom/document.h>
#include <kui/dom/element.h>
#include <kui/dom/html_lottie_element.h>
#include <kui/event/drag_event.h>
#include <kui/event/event_dispatcher.h>
#include <kui/extension/extension_manager.h>
#include <kui/layout/layout_box.h>
#include <kui/media/html_media_element.h>
#include <kui/net/http_client.h>
#include <kui/paint/display_list.h>
#include <kui/platform/platform.h>
#include <kui/platform/window.h>
#include <kui/render/render_backend.h>
#include <kui/render/renderer.h>
#ifdef KUI_HAS_SCRIPTING
#include <kui/script/script_engine.h>
#endif

#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

using kui_c::fromHandle;
using kui_c::str;
using kui_c::toHandle;
using kui_c::view;
using kui_c::writeString;

struct KuiHttpClient {
    kui::HttpClient value;
};

struct KuiHttpResponse {
    kui::HttpClient::Response value;
};

struct KuiExtensionResult {
    kui::ExtensionInvokeResult value;
};

struct KuiScriptFunction {
#ifdef KUI_HAS_SCRIPTING
    KuiScriptEngine* engine = nullptr;  // null once the engine is gone
    kui::ScriptValue::FunctionType fn;  // reset before the engine's Lua state closes
    bool owned = false;
#endif
};

struct KuiScriptEngine {
#ifdef KUI_HAS_SCRIPTING
    std::unique_ptr<kui::ScriptEngine> value;
    KuiScriptErrorFn errorFn = nullptr;
    void* errorUserData = nullptr;
    // Section 16 (3.61.8).
    int callbackDepth = 0;
    bool releasedInCallback = false;
    std::unordered_set<KuiScriptFunction*> functions;  // every live handle, owned or borrowed
    std::string resultText;                          // backs the string of the last result
    KuiScriptFunction* resultFunction = nullptr;     // backs its function
    std::string lastError;

    ~KuiScriptEngine() {
        // A Lua function handle must not outlive the Lua state it references.
        for (auto* function : functions) {
            function->fn = nullptr;
            function->engine = nullptr;
        }
        delete resultFunction;
    }
#endif
};

namespace {

inline kui::Window* window(KuiWindow* value) {
    return fromHandle<kui::Window>(value);
}

inline kui::Document* document(KuiDocument* value) {
    return fromHandle<kui::Document>(value);
}

inline kui::Element* element(KuiElement* value) {
    return fromHandle<kui::Element>(value);
}

inline kui::Event* event(KuiEvent* value) {
    return fromHandle<kui::Event>(value);
}

inline kui::HTMLMediaElement* media(KuiElement* value) {
    return dynamic_cast<kui::HTMLMediaElement*>(element(value));
}

inline kui::HTMLLottieElement* lottie(KuiElement* value) {
    return dynamic_cast<kui::HTMLLottieElement*>(element(value));
}

inline kui::ExtensionManager* extensions(KuiDocument* value) {
    return document(value)->extensionManager();
}

inline kui::Platform::FileDialogOptions fileDialogOptions(
        const KuiCFileDialogOptions* options) {
    kui::Platform::FileDialogOptions result;
    if (!options) return result;
    result.title = str(options->title, options->title_len);
    result.defaultPath = str(options->default_path, options->default_path_len);
    result.allowMultiple = options->allow_multiple != 0;
    if (options->filters) {
        result.filters.reserve(options->filter_count);
        for (size_t index = 0; index < options->filter_count; ++index) {
            if (options->filters[index]) result.filters.emplace_back(options->filters[index]);
        }
    }
    return result;
}

inline KuiStatus writeDialogResult(const kui::Optional<kui::String>& result,
                                   char* buf, size_t capacity, size_t* out_len) {
    if (!result) {
        if (out_len) *out_len = 0;
        if (buf && capacity) buf[0] = '\0';
        return KUI_ERR_NOT_FOUND;
    }
    return writeString(*result, buf, capacity, out_len);
}

inline std::vector<kui::ExtensionValue> extensionArgs(
        const KuiCExtensionValue* args, size_t count) {
    std::vector<kui::ExtensionValue> result;
    result.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        const auto& value = args[index];
        switch (value.type) {
        case KUI_C_EXT_INT:
            result.push_back(kui::ExtensionValue::integer(value.int_value));
            break;
        case KUI_C_EXT_FLOAT:
            result.push_back(kui::ExtensionValue::number(value.float_value));
            break;
        case KUI_C_EXT_STRING:
            result.push_back(kui::ExtensionValue::string(
                str(value.string_value, value.string_len)));
            break;
        case KUI_C_EXT_BOOL:
            result.push_back(kui::ExtensionValue::boolean(value.bool_value != 0));
            break;
        case KUI_C_EXT_NIL:
        default:
            result.push_back(kui::ExtensionValue::nullValue());
            break;
        }
    }
    return result;
}

inline KuiStatus requireExtensionValue(KuiExtensionResult* result, size_t index,
                                       const kui::ExtensionValue** out) {
    if (!result || !out) return KUI_ERR_INVALID_ARG;
    if (index >= result->value.values.size()) return KUI_ERR_NOT_FOUND;
    *out = &result->value.values[index];
    return KUI_OK;
}

}  // namespace

extern "C" {

KuiStatus kui_platform_get_monitor_count(size_t* out_count) {
    if (!out_count) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_count = kui::Platform::monitors().size();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_get_monitor(size_t index, KuiCMonitorInfo* out_monitor,
                                   char* name_buf, size_t name_capacity,
                                   size_t* out_name_len) {
    if (!out_monitor) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const auto monitors = kui::Platform::monitors();
        if (index >= monitors.size()) return KUI_ERR_NOT_FOUND;
        const auto& value = monitors[index];
        out_monitor->bounds_x = value.bounds.x;
        out_monitor->bounds_y = value.bounds.y;
        out_monitor->bounds_width = value.bounds.width;
        out_monitor->bounds_height = value.bounds.height;
        out_monitor->work_x = value.workArea.x;
        out_monitor->work_y = value.workArea.y;
        out_monitor->work_width = value.workArea.width;
        out_monitor->work_height = value.workArea.height;
        out_monitor->scale = value.scale;
        out_monitor->is_primary = value.isPrimary ? 1 : 0;
        return writeString(value.name, name_buf, name_capacity, out_name_len);
    KUI_C_END
}

KuiStatus kui_platform_get_primary_monitor_physical_size(float* out_width,
                                                         float* out_height) {
    if (!out_width && !out_height) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const auto value = kui::Platform::primaryMonitorPhysicalSize();
        if (out_width) *out_width = value.width;
        if (out_height) *out_height = value.height;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_get_clipboard_text(char* buf, size_t capacity, size_t* out_len) {
    KUI_C_BEGIN
        return writeString(kui::Platform::clipboardText(), buf, capacity, out_len);
    KUI_C_END
}

KuiStatus kui_platform_set_clipboard_text(const char* text, size_t len) {
    if (!text) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::Platform::setClipboardText(str(text, len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_has_clipboard_text(int* out_has_text) {
    if (!out_has_text) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_has_text = kui::Platform::hasClipboardText() ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_platform_get_os_name(char* buf, size_t capacity, size_t* out_len) {
    KUI_C_BEGIN
        return writeString(kui::Platform::osName(), buf, capacity, out_len);
    KUI_C_END
}

KuiStatus kui_platform_get_os_version(char* buf, size_t capacity, size_t* out_len) {
    KUI_C_BEGIN
        return writeString(kui::Platform::osVersion(), buf, capacity, out_len);
    KUI_C_END
}

KuiStatus kui_platform_open_file_dialog(const KuiCFileDialogOptions* options,
                                        char* buf, size_t capacity, size_t* out_len) {
    if (!buf || !capacity || !out_len) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        return writeDialogResult(kui::Platform::openFileDialog(fileDialogOptions(options)),
                                 buf, capacity, out_len);
    KUI_C_END
}

KuiStatus kui_platform_save_file_dialog(const KuiCFileDialogOptions* options,
                                        char* buf, size_t capacity, size_t* out_len) {
    if (!buf || !capacity || !out_len) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        return writeDialogResult(kui::Platform::saveFileDialog(fileDialogOptions(options)),
                                 buf, capacity, out_len);
    KUI_C_END
}

KuiStatus kui_platform_select_folder_dialog(const char* title, size_t title_len,
                                            char* buf, size_t capacity, size_t* out_len) {
    if (!buf || !capacity || !out_len) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        return writeDialogResult(kui::Platform::selectFolderDialog(str(title, title_len)),
                                 buf, capacity, out_len);
    KUI_C_END
}

KuiStatus kui_window_set_frameless_sizeable(KuiWindow* value, int enabled) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        window(value)->setFramelessSizeable(enabled != 0);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_set_frame_radius(KuiWindow* value, float radius) {
    if (!value || radius < 0) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        window(value)->setFrameRadius(radius);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_clear_frame_radius(KuiWindow* value) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        window(value)->clearFrameRadius();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_get_devtools_port(KuiWindow* value, uint16_t* out_port) {
    if (!value || !out_port) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* server = window(value)->devToolsServer();
        if (!server) return KUI_ERR_NOT_FOUND;
        *out_port = server->port();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_add_event_listener(KuiWindow* value, const char* event_name,
                                        size_t len, KuiWindowEventFn fn,
                                        void* user_data, int once,
                                        KuiListenerId* out_id) {
    if (!value || !event_name || !fn) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const auto id = window(value)->addEventListener(
            str(event_name, len),
            [fn, user_data](kui::Event& nativeEvent) {
                try { fn(toHandle<KuiEvent>(&nativeEvent), user_data); }
                catch (...) {}
            }, once != 0);
        if (out_id) *out_id = id;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_remove_event_listener(KuiWindow* value, const char* event_name,
                                           size_t len, KuiListenerId id) {
    if (!value || !event_name) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        window(value)->removeEventListener(str(event_name, len),
                                           static_cast<kui::u32>(id));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_remove_event_listeners(KuiWindow* value, const char* event_name,
                                            size_t len) {
    if (!value || !event_name) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        window(value)->removeEventListeners(str(event_name, len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_event_get_type_tag(KuiEvent* value, KuiCEventTypeTag* out_tag) {
    if (!value || !out_tag) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_tag = static_cast<KuiCEventTypeTag>(event(value)->typeTag());
        return KUI_OK;
    KUI_C_END
}

// --- Section 15 (3.61.8): drag-and-drop data -------------------------------------
namespace {
kui::DataTransfer* dataTransfer(KuiEvent* value) {
    auto* drag = dynamic_cast<kui::DragEvent*>(event(value));
    return drag ? drag->dataTransfer() : nullptr;
}
}  // namespace

KuiStatus kui_event_drag_set_data(KuiEvent* value, const char* format, size_t format_len,
                                  const char* data, size_t data_len) {
    if (!value || !format || !data) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* transfer = dataTransfer(value);
        if (!transfer) return KUI_ERR_NOT_SUPPORTED;
        const std::string name = str(format, format_len);
        if (name.empty()) return KUI_ERR_INVALID_ARG;
        transfer->setData(name, str(data, data_len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_event_drag_get_data(KuiEvent* value, const char* format, size_t format_len, char* buf,
                                  size_t buf_capacity, size_t* out_len) {
    if (!value || !format) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* transfer = dataTransfer(value);
        if (!transfer) return KUI_ERR_NOT_SUPPORTED;
        const std::string name = str(format, format_len);
        if (!transfer->hasData(name)) return KUI_ERR_NOT_FOUND;
        return kui_c::writeString(transfer->getData(name), buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_event_drag_clear_data(KuiEvent* value, const char* format, size_t format_len) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* transfer = dataTransfer(value);
        if (!transfer) return KUI_ERR_NOT_SUPPORTED;
        if (format) transfer->clearData(str(format, format_len));
        else transfer->clearData();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_event_drag_get_type_count(KuiEvent* value, size_t* out_count) {
    if (!value || !out_count) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* transfer = dataTransfer(value);
        if (!transfer) return KUI_ERR_NOT_SUPPORTED;
        *out_count = transfer->types().size();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_event_drag_get_type(KuiEvent* value, size_t index, char* buf, size_t buf_capacity,
                                  size_t* out_len) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* transfer = dataTransfer(value);
        if (!transfer) return KUI_ERR_NOT_SUPPORTED;
        if (index >= transfer->types().size()) return KUI_ERR_NOT_FOUND;
        return kui_c::writeString(transfer->types()[index], buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_event_drag_set_drop_effect(KuiEvent* value, KuiCDropEffect effect) {
    if (!value || effect < KUI_C_DROP_NONE || effect > KUI_C_DROP_LINK) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* transfer = dataTransfer(value);
        if (!transfer) return KUI_ERR_NOT_SUPPORTED;
        transfer->setDropEffect(static_cast<kui::DropEffect>(effect));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_event_drag_get_drop_effect(KuiEvent* value, KuiCDropEffect* out_effect) {
    if (!value || !out_effect) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* transfer = dataTransfer(value);
        if (!transfer) return KUI_ERR_NOT_SUPPORTED;
        *out_effect = static_cast<KuiCDropEffect>(transfer->dropEffect());
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_event_drag_set_effect_allowed(KuiEvent* value, KuiCEffectAllowed effect) {
    if (!value || effect < KUI_C_EFFECT_NONE || effect > KUI_C_EFFECT_UNINITIALIZED) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* transfer = dataTransfer(value);
        if (!transfer) return KUI_ERR_NOT_SUPPORTED;
        transfer->setEffectAllowed(static_cast<kui::EffectAllowed>(effect));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_event_drag_get_effect_allowed(KuiEvent* value, KuiCEffectAllowed* out_effect) {
    if (!value || !out_effect) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* transfer = dataTransfer(value);
        if (!transfer) return KUI_ERR_NOT_SUPPORTED;
        *out_effect = static_cast<KuiCEffectAllowed>(transfer->effectAllowed());
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_event_drag_get_file_count(KuiEvent* value, size_t* out_count) {
    if (!value || !out_count) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* drag = dynamic_cast<kui::DragEvent*>(event(value));
        if (!drag || !drag->dataTransfer()) return KUI_ERR_NOT_SUPPORTED;
        *out_count = drag->dataTransfer()->files().size();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_event_drag_get_file(KuiEvent* value, size_t index,
                                  char* buf, size_t capacity, size_t* out_len) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* drag = dynamic_cast<kui::DragEvent*>(event(value));
        if (!drag || !drag->dataTransfer()) return KUI_ERR_NOT_SUPPORTED;
        const auto& files = drag->dataTransfer()->files();
        if (index >= files.size()) return KUI_ERR_NOT_FOUND;
        return writeString(files[index], buf, capacity, out_len);
    KUI_C_END
}

KuiStatus kui_element_position_relative_to(KuiElement* value, KuiElement* anchor,
                                           const char* placement, size_t placement_len,
                                           const KuiCPositioningOptions* options) {
    if (!value || !anchor || !placement) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::Element::PositioningOptions native;
        if (options) {
            native.flip = options->flip != 0;
            native.clampToViewport = options->clamp_to_viewport != 0;
            native.offset = options->offset;
            native.viewportMargin = options->viewport_margin;
        }
        element(value)->positionRelativeTo(element(anchor), str(placement, placement_len), native);
        return KUI_OK;
    KUI_C_END
}

#define KUI_C_MEDIA_REQUIRE(value) \
    auto* nativeMedia = media(value); \
    if (!nativeMedia) return KUI_ERR_NOT_SUPPORTED

KuiStatus kui_media_play(KuiElement* value) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_MEDIA_REQUIRE(value); nativeMedia->play(); return KUI_OK; KUI_C_END
}
KuiStatus kui_media_pause(KuiElement* value) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_MEDIA_REQUIRE(value); nativeMedia->pause(); return KUI_OK; KUI_C_END
}
KuiStatus kui_media_set_src(KuiElement* value, const char* src, size_t len) {
    if (!value || !src) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_MEDIA_REQUIRE(value); nativeMedia->setSrc(str(src, len)); return KUI_OK; KUI_C_END
}
KuiStatus kui_media_set_controls(KuiElement* value, int enabled) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_MEDIA_REQUIRE(value); nativeMedia->setControls(enabled != 0); return KUI_OK; KUI_C_END
}
KuiStatus kui_media_set_current_time(KuiElement* value, double seconds) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_MEDIA_REQUIRE(value); nativeMedia->setCurrentTime(seconds); return KUI_OK; KUI_C_END
}
KuiStatus kui_media_get_current_time(KuiElement* value, double* out_seconds) {
    if (!value || !out_seconds) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_MEDIA_REQUIRE(value); *out_seconds = nativeMedia->currentTime(); return KUI_OK; KUI_C_END
}
KuiStatus kui_media_get_duration(KuiElement* value, double* out_seconds) {
    if (!value || !out_seconds) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_MEDIA_REQUIRE(value); *out_seconds = nativeMedia->duration(); return KUI_OK; KUI_C_END
}
KuiStatus kui_media_set_volume(KuiElement* value, float volume) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_MEDIA_REQUIRE(value); nativeMedia->setVolume(volume); return KUI_OK; KUI_C_END
}
KuiStatus kui_media_get_volume(KuiElement* value, float* out_volume) {
    if (!value || !out_volume) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_MEDIA_REQUIRE(value); *out_volume = nativeMedia->volume(); return KUI_OK; KUI_C_END
}
KuiStatus kui_media_set_muted(KuiElement* value, int muted) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_MEDIA_REQUIRE(value); nativeMedia->setMuted(muted != 0); return KUI_OK; KUI_C_END
}
KuiStatus kui_media_get_muted(KuiElement* value, int* out_muted) {
    if (!value || !out_muted) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_MEDIA_REQUIRE(value); *out_muted = nativeMedia->muted() ? 1 : 0; return KUI_OK; KUI_C_END
}
KuiStatus kui_media_get_paused(KuiElement* value, int* out_paused) {
    if (!value || !out_paused) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_MEDIA_REQUIRE(value); *out_paused = nativeMedia->paused() ? 1 : 0; return KUI_OK; KUI_C_END
}
KuiStatus kui_media_get_ready_state(KuiElement* value, int* out_state) {
    if (!value || !out_state) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_MEDIA_REQUIRE(value); *out_state = static_cast<int>(nativeMedia->readyState()); return KUI_OK; KUI_C_END
}
KuiStatus kui_media_get_network_state(KuiElement* value, int* out_state) {
    if (!value || !out_state) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_MEDIA_REQUIRE(value); *out_state = static_cast<int>(nativeMedia->networkState()); return KUI_OK; KUI_C_END
}
KuiStatus kui_media_is_available(KuiElement* value, int* out_available) {
    if (!value || !out_available) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_MEDIA_REQUIRE(value); *out_available = nativeMedia->isMediaAvailable() ? 1 : 0; return KUI_OK; KUI_C_END
}

#undef KUI_C_MEDIA_REQUIRE

#define KUI_C_LOTTIE_REQUIRE(value) \
    auto* nativeLottie = lottie(value); \
    if (!nativeLottie) return KUI_ERR_NOT_SUPPORTED

KuiStatus kui_lottie_set_src(KuiElement* value, const char* src, size_t len) {
    if (!value || !src) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_LOTTIE_REQUIRE(value); nativeLottie->setSrc(str(src, len)); return KUI_OK; KUI_C_END
}
KuiStatus kui_lottie_play(KuiElement* value) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_LOTTIE_REQUIRE(value); nativeLottie->play(); return KUI_OK; KUI_C_END
}
KuiStatus kui_lottie_pause(KuiElement* value) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_LOTTIE_REQUIRE(value); nativeLottie->pause(); return KUI_OK; KUI_C_END
}
KuiStatus kui_lottie_get_complete(KuiElement* value, int* out_complete) {
    if (!value || !out_complete) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN KUI_C_LOTTIE_REQUIRE(value); *out_complete = nativeLottie->complete() ? 1 : 0; return KUI_OK; KUI_C_END
}
KuiStatus kui_lottie_get_natural_size(KuiElement* value, float* out_width, float* out_height) {
    if (!value || (!out_width && !out_height)) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        KUI_C_LOTTIE_REQUIRE(value);
        if (out_width) *out_width = static_cast<float>(nativeLottie->naturalWidth());
        if (out_height) *out_height = static_cast<float>(nativeLottie->naturalHeight());
        return KUI_OK;
    KUI_C_END
}

// --- Section 13 (3.61.8): media and Lottie playback control ---------------------
KuiStatus kui_media_load(KuiElement* value) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN auto* nativeMedia = media(value); if (!nativeMedia) return KUI_ERR_NOT_SUPPORTED; nativeMedia->load(); return KUI_OK; KUI_C_END
}
KuiStatus kui_media_set_loop(KuiElement* value, int loop) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN auto* nativeMedia = media(value); if (!nativeMedia) return KUI_ERR_NOT_SUPPORTED; nativeMedia->setLoop(loop != 0); return KUI_OK; KUI_C_END
}
KuiStatus kui_media_set_autoplay(KuiElement* value, int autoplay) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN auto* nativeMedia = media(value); if (!nativeMedia) return KUI_ERR_NOT_SUPPORTED; nativeMedia->setAutoplay(autoplay != 0); return KUI_OK; KUI_C_END
}
KuiStatus kui_media_get_playback_flags(KuiElement* value, int* out_loop, int* out_autoplay,
                                       int* out_ended) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* nativeMedia = media(value); if (!nativeMedia) return KUI_ERR_NOT_SUPPORTED;
        if (out_loop) *out_loop = nativeMedia->loop() ? 1 : 0;
        if (out_autoplay) *out_autoplay = nativeMedia->autoplay() ? 1 : 0;
        if (out_ended) *out_ended = nativeMedia->ended() ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}
KuiStatus kui_lottie_stop(KuiElement* value) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN auto* nativeLottie = lottie(value); if (!nativeLottie) return KUI_ERR_NOT_SUPPORTED; nativeLottie->stop(); return KUI_OK; KUI_C_END
}
KuiStatus kui_lottie_seek(KuiElement* value, double position) {
    if (!value || !(position >= 0.0 && position <= 1.0)) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN auto* nativeLottie = lottie(value); if (!nativeLottie) return KUI_ERR_NOT_SUPPORTED; nativeLottie->seek(position); return KUI_OK; KUI_C_END
}
KuiStatus kui_lottie_set_speed(KuiElement* value, float speed) {
    if (!value || !(speed >= 0.0f)) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* nativeLottie = lottie(value); if (!nativeLottie) return KUI_ERR_NOT_SUPPORTED;
        char text[32];
        std::snprintf(text, sizeof text, "%g", static_cast<double>(speed));
        nativeLottie->setAttribute("speed", text);
        return KUI_OK;
    KUI_C_END
}
KuiStatus kui_lottie_set_loop(KuiElement* value, int loop) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* nativeLottie = lottie(value); if (!nativeLottie) return KUI_ERR_NOT_SUPPORTED;
        if (loop) nativeLottie->setAttribute("loop", "");
        else nativeLottie->removeAttribute("loop");
        return KUI_OK;
    KUI_C_END
}
KuiStatus kui_lottie_get_state(KuiElement* value, KuiCLottieState* out_state) {
    if (!value || !out_state) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* nativeLottie = lottie(value); if (!nativeLottie) return KUI_ERR_NOT_SUPPORTED;
        out_state->playing = nativeLottie->isPlaying() ? 1 : 0;
        out_state->looping = nativeLottie->loop() ? 1 : 0;
        out_state->autoplay = nativeLottie->autoplay() ? 1 : 0;
        out_state->speed = nativeLottie->speed();
        out_state->position = nativeLottie->currentPosition();
        out_state->load_state = static_cast<KuiCLottieLoadState>(nativeLottie->loadState());
        return KUI_OK;
    KUI_C_END
}

#undef KUI_C_LOTTIE_REQUIRE

KuiStatus kui_script_engine_create(KuiScriptEngine** out_engine) {
    if (!out_engine) return KUI_ERR_INVALID_ARG;
    *out_engine = nullptr;
#ifdef KUI_HAS_SCRIPTING
    KUI_C_BEGIN
        auto result = std::make_unique<KuiScriptEngine>();
        result->value = kui::ScriptEngine::create();
        if (!result->value) return KUI_ERR_NOT_SUPPORTED;
        *out_engine = result.release();
        return KUI_OK;
    KUI_C_END
#else
    return KUI_ERR_NOT_SUPPORTED;
#endif
}

KuiStatus kui_script_engine_release(KuiScriptEngine* engine) {
    if (!engine) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
#ifdef KUI_HAS_SCRIPTING
        if (engine->callbackDepth > 0) {
            engine->releasedInCallback = true;  // freed after the callback returns
            return KUI_OK;
        }
#endif
        delete engine;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_script_engine_bind_document(KuiScriptEngine* engine,
                                          KuiDocument* doc) {
    if (!engine || !doc) return KUI_ERR_INVALID_ARG;
#ifdef KUI_HAS_SCRIPTING
    KUI_C_BEGIN engine->value->bindDocument(document(doc)); return KUI_OK; KUI_C_END
#else
    return KUI_ERR_NOT_SUPPORTED;
#endif
}

KuiStatus kui_script_engine_unbind_document(KuiScriptEngine* engine) {
    if (!engine) return KUI_ERR_INVALID_ARG;
#ifdef KUI_HAS_SCRIPTING
    KUI_C_BEGIN engine->value->unbindDocument(); return KUI_OK; KUI_C_END
#else
    return KUI_ERR_NOT_SUPPORTED;
#endif
}

KuiStatus kui_script_engine_eval(KuiScriptEngine* engine, const char* code,
                                 size_t code_len, const char* source,
                                 size_t source_len, int* out_success,
                                 char* error_buf, size_t error_capacity,
                                 size_t* out_error_len) {
    if (!engine || !code) return KUI_ERR_INVALID_ARG;
#ifdef KUI_HAS_SCRIPTING
    KUI_C_BEGIN
        const auto result = engine->value->eval(str(code, code_len),
                                                source ? str(source, source_len) : "<eval>");
        engine->lastError = result.error;
        if (out_success) *out_success = result.success ? 1 : 0;
        const auto status = writeString(result.error, error_buf, error_capacity, out_error_len);
        return status;
    KUI_C_END
#else
    (void)code_len; (void)source; (void)source_len; (void)out_success;
    (void)error_buf; (void)error_capacity; (void)out_error_len;
    return KUI_ERR_NOT_SUPPORTED;
#endif
}

KuiStatus kui_script_engine_execute_scripts(KuiScriptEngine* engine) {
    if (!engine) return KUI_ERR_INVALID_ARG;
#ifdef KUI_HAS_SCRIPTING
    KUI_C_BEGIN engine->value->executeScripts(); return KUI_OK; KUI_C_END
#else
    return KUI_ERR_NOT_SUPPORTED;
#endif
}

KuiStatus kui_script_engine_set_error_handler(KuiScriptEngine* engine,
                                              KuiScriptErrorFn fn,
                                              void* user_data) {
    if (!engine) return KUI_ERR_INVALID_ARG;
#ifdef KUI_HAS_SCRIPTING
    KUI_C_BEGIN
        engine->errorFn = fn;
        engine->errorUserData = user_data;
        if (!fn) {
            engine->value->setErrorHandler({});
        } else {
            engine->value->setErrorHandler([engine](const kui::String& error,
                                                     const kui::String& source,
                                                     int line) {
                if (engine->errorFn) engine->errorFn(
                    error.data(), error.size(), source.data(), source.size(), line,
                    engine->errorUserData);
            });
        }
        return KUI_OK;
    KUI_C_END
#else
    (void)fn; (void)user_data;
    return KUI_ERR_NOT_SUPPORTED;
#endif
}

KuiStatus kui_window_set_script_engine(KuiWindow* value, KuiScriptEngine* engine) {
    if (!value) return KUI_ERR_INVALID_ARG;
#ifdef KUI_HAS_SCRIPTING
    KUI_C_BEGIN
        window(value)->setScriptEngine(engine ? engine->value.get() : nullptr);
        return KUI_OK;
    KUI_C_END
#else
    (void)engine;
    return KUI_ERR_NOT_SUPPORTED;
#endif
}

// --- Section 16 (3.61.8): script engine control ------------------------------------
#ifdef KUI_HAS_SCRIPTING
}  // extern "C": the helpers below return C++ types

namespace {

// Thrown by a C function's wrapper to turn its error status into a Lua error (sol2
// converts the exception at the Lua call boundary).
struct ScriptCallbackError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

KuiScriptFunction* newFunction(KuiScriptEngine* engine, kui::ScriptValue::FunctionType fn,
                               bool owned) {
    auto function = std::make_unique<KuiScriptFunction>();
    function->engine = engine;
    function->fn = std::move(fn);
    function->owned = owned;
    engine->functions.insert(function.get());
    return function.release();
}

void deleteFunction(KuiScriptFunction* function) {
    if (function->engine) function->engine->functions.erase(function);
    delete function;
}

// Function handles lent to a C callback for the duration of one call.
struct BorrowedFunctions {
    std::vector<KuiScriptFunction*> items;
    BorrowedFunctions() = default;
    BorrowedFunctions(const BorrowedFunctions&) = delete;
    BorrowedFunctions& operator=(const BorrowedFunctions&) = delete;
    ~BorrowedFunctions() {
        for (auto* function : items) deleteFunction(function);
    }
};

// C -> native. False for a malformed value (unknown type, dead function handle).
bool toNative(const KuiCScriptValue& value, kui::ScriptValue& out) {
    switch (value.type) {
        case KUI_C_SCRIPT_NIL: out = kui::ScriptValue::nil(); return true;
        case KUI_C_SCRIPT_BOOL: out = kui::ScriptValue::from(value.bool_value != 0); return true;
        case KUI_C_SCRIPT_INT:
            out = kui::ScriptValue::from(static_cast<kui::i64>(value.int_value));
            return true;
        case KUI_C_SCRIPT_FLOAT:
            out = kui::ScriptValue::from(static_cast<kui::f64>(value.float_value));
            return true;
        case KUI_C_SCRIPT_STRING:
            out = kui::ScriptValue::from(str(value.string_value, value.string_len));
            return true;
        case KUI_C_SCRIPT_FUNCTION:
            if (!value.function_value || !value.function_value->fn) return false;
            out = kui::ScriptValue::fromFunction(value.function_value->fn);
            return true;
        case KUI_C_SCRIPT_USERDATA: out = kui::ScriptValue::fromUserData(value.userdata); return true;
    }
    return false;
}

bool argsToNative(const KuiCScriptValue* args, size_t count, std::vector<kui::ScriptValue>& out) {
    if (count && !args) return false;
    out.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        kui::ScriptValue value;
        if (!toNative(args[index], value)) return false;
        out.push_back(std::move(value));
    }
    return true;
}

// native -> C without copying: the string points into `value`.
void fillBorrowed(const kui::ScriptValue& value, KuiCScriptValue& out, KuiScriptFunction* function) {
    out = KuiCScriptValue{};
    switch (value.type) {
        case kui::ScriptValue::Nil: out.type = KUI_C_SCRIPT_NIL; break;
        case kui::ScriptValue::Bool:
            out.type = KUI_C_SCRIPT_BOOL;
            out.bool_value = value.asBool() ? 1 : 0;
            break;
        case kui::ScriptValue::Int:
            out.type = KUI_C_SCRIPT_INT;
            out.int_value = value.asInt();
            break;
        case kui::ScriptValue::Float:
            out.type = KUI_C_SCRIPT_FLOAT;
            out.float_value = value.asFloat();
            break;
        case kui::ScriptValue::String:
            out.type = KUI_C_SCRIPT_STRING;
            out.string_value = value.asString().c_str();
            out.string_len = value.asString().size();
            break;
        case kui::ScriptValue::Function:
            out.type = KUI_C_SCRIPT_FUNCTION;
            out.function_value = function;
            break;
        case kui::ScriptValue::UserData:
            out.type = KUI_C_SCRIPT_USERDATA;
            out.userdata = value.asUserData();
            break;
    }
}

// The value an engine call returns: kept by the engine until its next result.
void setResult(KuiScriptEngine* engine, const kui::ScriptValue& value, KuiCScriptValue* out) {
    if (engine->resultFunction) {
        deleteFunction(engine->resultFunction);
        engine->resultFunction = nullptr;
    }
    engine->resultText.clear();
    if (!out) return;
    if (value.type == kui::ScriptValue::Function) {
        engine->resultFunction = newFunction(engine, value.asFunction(), false);
    }
    fillBorrowed(value, *out, engine->resultFunction);
    if (value.type == kui::ScriptValue::String) {
        engine->resultText = value.asString();
        out->string_value = engine->resultText.c_str();
        out->string_len = engine->resultText.size();
    }
}

void finishRun(KuiScriptEngine* engine, const kui::ScriptResult& result, int* out_success,
               KuiCScriptValue* out_value) {
    engine->lastError = result.error;
    if (out_success) *out_success = result.success ? 1 : 0;
    setResult(engine, result.value, out_value);
}

kui::ScriptValue::FunctionType wrapCFunction(KuiScriptEngine* engine, KuiScriptFunctionFn fn,
                                             kui_c::UserDataPtr holder) {
    return [engine, fn, holder](const std::vector<kui::ScriptValue>& args) -> kui::ScriptValue {
        // Locals only from here on: the callback may replace (destroy) this function.
        kui_c::UserDataPtr keep = holder;
        KuiScriptEngine* owner = engine;
        KuiScriptFunctionFn call = fn;
        kui_c::CallbackScope<KuiScriptEngine> scope(owner);
        kui::ScriptValue native;
        bool valid = false;
        KuiStatus status = KUI_OK;
        std::string message;
        {
            BorrowedFunctions borrowed;
            std::vector<KuiCScriptValue> cArgs(args.size());
            for (size_t index = 0; index < args.size(); ++index) {
                KuiScriptFunction* function = nullptr;
                if (args[index].type == kui::ScriptValue::Function) {
                    function = newFunction(owner, args[index].asFunction(), false);
                    borrowed.items.push_back(function);
                }
                fillBorrowed(args[index], cArgs[index], function);
            }
            KuiCScriptValue result{};
            status = call(cArgs.empty() ? nullptr : cArgs.data(), cArgs.size(), &result, keep->data);
            if (status != KUI_OK) {
                message = result.type == KUI_C_SCRIPT_STRING && result.string_value
                              ? str(result.string_value, result.string_len)
                              : std::string("C function failed: ") + kui_status_message(status);
            } else {
                valid = toNative(result, native);
            }
        }
        if (status != KUI_OK) throw ScriptCallbackError(message);
        if (!valid) throw ScriptCallbackError("C function returned an invalid script value");
        return native;
    };
}

}  // namespace

extern "C" {

KuiStatus kui_script_engine_eval_value(KuiScriptEngine* engine, const char* code, size_t code_len,
                                       const char* source, size_t source_len, int* out_success,
                                       KuiCScriptValue* out_value) {
    if (!engine || !code) return KUI_ERR_INVALID_ARG;
    if (out_success) *out_success = 0;
    KUI_C_BEGIN
        const auto result = engine->value->eval(str(code, code_len),
                                                source ? str(source, source_len) : "<eval>");
        finishRun(engine, result, out_success, out_value);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_script_engine_tick_timers(KuiScriptEngine* engine, double now_ms) {
    if (!engine) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        engine->value->tickTimers(now_ms);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_script_engine_eval_file(KuiScriptEngine* engine, const char* path, size_t path_len,
                                      int* out_success, KuiCScriptValue* out_value) {
    if (!engine || !path) return KUI_ERR_INVALID_ARG;
    if (out_success) *out_success = 0;
    KUI_C_BEGIN
        const std::string file = str(path, path_len);
        if (file.empty()) return KUI_ERR_INVALID_ARG;
        finishRun(engine, engine->value->evalFile(file), out_success, out_value);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_script_engine_call_function(KuiScriptEngine* engine, const char* name,
                                          size_t name_len, const KuiCScriptValue* args,
                                          size_t arg_count, int* out_success,
                                          KuiCScriptValue* out_value) {
    if (!engine || !name) return KUI_ERR_INVALID_ARG;
    if (out_success) *out_success = 0;
    KUI_C_BEGIN
        const std::string global = str(name, name_len);
        std::vector<kui::ScriptValue> nativeArgs;
        if (global.empty() || !argsToNative(args, arg_count, nativeArgs)) return KUI_ERR_INVALID_ARG;
        finishRun(engine, engine->value->callFunction(global, nativeArgs), out_success, out_value);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_script_engine_get_last_error(KuiScriptEngine* engine, char* buf, size_t buf_capacity,
                                           size_t* out_len) {
    if (!engine) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN return writeString(engine->lastError, buf, buf_capacity, out_len); KUI_C_END
}

KuiStatus kui_script_engine_register_function(KuiScriptEngine* engine, const char* name,
                                              size_t name_len, KuiScriptFunctionFn fn,
                                              void* user_data, KuiTaskFn release_fn) {
    if (!engine || !name || !fn) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const std::string global = str(name, name_len);
        if (global.empty()) return KUI_ERR_INVALID_ARG;
        kui_c::ArmOnSuccess guard(std::make_shared<kui_c::UserData>(user_data, release_fn));
        engine->value->registerFunction(global, wrapCFunction(engine, fn, guard.holder));
        guard.commit();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_script_engine_set_global(KuiScriptEngine* engine, const char* name, size_t name_len,
                                       const KuiCScriptValue* value) {
    if (!engine || !name || !value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const std::string global = str(name, name_len);
        kui::ScriptValue native;
        if (global.empty() || !toNative(*value, native)) return KUI_ERR_INVALID_ARG;
        engine->value->setGlobal(global, std::move(native));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_script_engine_get_global(KuiScriptEngine* engine, const char* name, size_t name_len,
                                       KuiCScriptValue* out_value) {
    if (!engine || !name || !out_value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const std::string global = str(name, name_len);
        if (global.empty()) return KUI_ERR_INVALID_ARG;
        setResult(engine, engine->value->getGlobal(global), out_value);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_script_engine_set_print_handler(KuiScriptEngine* engine, KuiScriptPrintFn fn,
                                              void* user_data, KuiTaskFn release_fn) {
    if (!engine) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        if (!fn) {
            engine->value->setPrintHandler({});
            return KUI_OK;
        }
        kui_c::ArmOnSuccess guard(std::make_shared<kui_c::UserData>(user_data, release_fn));
        kui_c::UserDataPtr holder = guard.holder;
        engine->value->setPrintHandler([engine, fn, holder](const kui::String& message) {
            // Locals only: the handler may replace (destroy) this lambda.
            kui_c::UserDataPtr keep = holder;
            KuiScriptPrintFn call = fn;
            kui_c::CallbackScope<KuiScriptEngine> scope(engine);
            call(message.data(), message.size(), keep->data);
        });
        guard.commit();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_script_engine_set_memory_limit(KuiScriptEngine* engine, size_t bytes) {
    if (!engine) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN engine->value->setMemoryLimit(bytes); return KUI_OK; KUI_C_END
}

KuiStatus kui_script_engine_set_instruction_limit(KuiScriptEngine* engine, uint64_t max_instructions) {
    if (!engine) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN engine->value->setInstructionLimit(max_instructions); return KUI_OK; KUI_C_END
}

KuiStatus kui_script_engine_get_resource_usage(KuiScriptEngine* engine, size_t* out_memory_bytes,
                                               size_t* out_memory_limit,
                                               uint64_t* out_instruction_limit) {
    if (!engine) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        if (out_memory_bytes) *out_memory_bytes = engine->value->memoryUsageBytes();
        if (out_memory_limit) {
            const size_t limit = engine->value->memoryLimitBytes();
            *out_memory_limit = limit == SIZE_MAX ? 0 : limit;  // native reports "none" as SIZE_MAX
        }
        if (out_instruction_limit) *out_instruction_limit = engine->value->instructionLimit();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_script_function_create(KuiScriptEngine* engine, KuiScriptFunctionFn fn,
                                     void* user_data, KuiTaskFn release_fn,
                                     KuiScriptFunction** out_function) {
    if (!engine || !fn || !out_function) return KUI_ERR_INVALID_ARG;
    *out_function = nullptr;
    KUI_C_BEGIN
        kui_c::ArmOnSuccess guard(std::make_shared<kui_c::UserData>(user_data, release_fn));
        auto* function = newFunction(engine, wrapCFunction(engine, fn, guard.holder), true);
        guard.commit();
        *out_function = function;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_script_function_call(KuiScriptFunction* function, const KuiCScriptValue* args,
                                   size_t arg_count, int* out_success, KuiCScriptValue* out_value) {
    if (!function) return KUI_ERR_INVALID_ARG;
    if (out_success) *out_success = 0;
    KUI_C_BEGIN
        KuiScriptEngine* engine = function->engine;
        if (!engine || !function->fn) return KUI_ERR_INVALID_ARG;
        std::vector<kui::ScriptValue> nativeArgs;
        if (!argsToNative(args, arg_count, nativeArgs)) return KUI_ERR_INVALID_ARG;
        // A copy: the handle may be the engine's last result, replaced during the call.
        const auto fn = function->fn;
        kui::ScriptValue result;
        try {
            result = fn(nativeArgs);
        } catch (const ScriptCallbackError& error) {
            engine->lastError = error.what();
            setResult(engine, kui::ScriptValue::nil(), out_value);
            return KUI_OK;
        }
        engine->lastError.clear();
        if (out_success) *out_success = 1;
        setResult(engine, result, out_value);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_script_function_retain(KuiScriptFunction* function, KuiScriptFunction** out_owned) {
    if (!function || !out_owned) return KUI_ERR_INVALID_ARG;
    *out_owned = nullptr;
    KUI_C_BEGIN
        if (!function->engine || !function->fn) return KUI_ERR_INVALID_ARG;
        *out_owned = newFunction(function->engine, function->fn, true);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_script_function_release(KuiScriptFunction* function) {
    if (!function || !function->owned) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN deleteFunction(function); return KUI_OK; KUI_C_END
}
#else   // !KUI_HAS_SCRIPTING
KuiStatus kui_script_engine_eval_value(KuiScriptEngine*, const char*, size_t, const char*, size_t,
                                       int*, KuiCScriptValue*) { return KUI_ERR_NOT_SUPPORTED; }
KuiStatus kui_script_engine_tick_timers(KuiScriptEngine*, double) { return KUI_ERR_NOT_SUPPORTED; }
KuiStatus kui_script_engine_eval_file(KuiScriptEngine*, const char*, size_t, int*,
                                      KuiCScriptValue*) { return KUI_ERR_NOT_SUPPORTED; }
KuiStatus kui_script_engine_call_function(KuiScriptEngine*, const char*, size_t,
                                          const KuiCScriptValue*, size_t, int*,
                                          KuiCScriptValue*) { return KUI_ERR_NOT_SUPPORTED; }
KuiStatus kui_script_engine_get_last_error(KuiScriptEngine*, char*, size_t, size_t*) {
    return KUI_ERR_NOT_SUPPORTED;
}
KuiStatus kui_script_engine_register_function(KuiScriptEngine*, const char*, size_t,
                                              KuiScriptFunctionFn, void*, KuiTaskFn) {
    return KUI_ERR_NOT_SUPPORTED;
}
KuiStatus kui_script_engine_set_global(KuiScriptEngine*, const char*, size_t,
                                       const KuiCScriptValue*) { return KUI_ERR_NOT_SUPPORTED; }
KuiStatus kui_script_engine_get_global(KuiScriptEngine*, const char*, size_t, KuiCScriptValue*) {
    return KUI_ERR_NOT_SUPPORTED;
}
KuiStatus kui_script_engine_set_print_handler(KuiScriptEngine*, KuiScriptPrintFn, void*, KuiTaskFn) {
    return KUI_ERR_NOT_SUPPORTED;
}
KuiStatus kui_script_engine_set_memory_limit(KuiScriptEngine*, size_t) { return KUI_ERR_NOT_SUPPORTED; }
KuiStatus kui_script_engine_set_instruction_limit(KuiScriptEngine*, uint64_t) {
    return KUI_ERR_NOT_SUPPORTED;
}
KuiStatus kui_script_engine_get_resource_usage(KuiScriptEngine*, size_t*, size_t*, uint64_t*) {
    return KUI_ERR_NOT_SUPPORTED;
}
KuiStatus kui_script_function_create(KuiScriptEngine*, KuiScriptFunctionFn, void*, KuiTaskFn,
                                     KuiScriptFunction** out_function) {
    if (out_function) *out_function = nullptr;
    return KUI_ERR_NOT_SUPPORTED;
}
KuiStatus kui_script_function_call(KuiScriptFunction*, const KuiCScriptValue*, size_t, int*,
                                   KuiCScriptValue*) { return KUI_ERR_NOT_SUPPORTED; }
KuiStatus kui_script_function_retain(KuiScriptFunction*, KuiScriptFunction** out_owned) {
    if (out_owned) *out_owned = nullptr;
    return KUI_ERR_NOT_SUPPORTED;
}
KuiStatus kui_script_function_release(KuiScriptFunction*) { return KUI_ERR_NOT_SUPPORTED; }
#endif  // KUI_HAS_SCRIPTING

KuiStatus kui_http_client_create(KuiHttpClient** out_client) {
    if (!out_client) return KUI_ERR_INVALID_ARG;
    *out_client = nullptr;
    KUI_C_BEGIN *out_client = new KuiHttpClient(); return KUI_OK; KUI_C_END
}

KuiStatus kui_http_client_release(KuiHttpClient* client) {
    if (!client) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN delete client; return KUI_OK; KUI_C_END
}

KuiStatus kui_http_client_set_user_agent(KuiHttpClient* client,
                                         const char* user_agent, size_t len) {
    if (!client || !user_agent) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN client->value.setUserAgent(str(user_agent, len)); return KUI_OK; KUI_C_END
}

KuiStatus kui_http_client_fetch_sync(KuiHttpClient* client, const char* url,
                                     size_t len, double timeout_seconds,
                                     KuiHttpResponse** out_response) {
    if (!client || !url || !out_response) return KUI_ERR_INVALID_ARG;
    *out_response = nullptr;
    KUI_C_BEGIN
        auto result = std::make_unique<KuiHttpResponse>();
        result->value = client->value.fetchSync(str(url, len), timeout_seconds);
        *out_response = result.release();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_http_client_fetch_async(KuiHttpClient* client, const char* url,
                                      size_t len, double timeout_seconds,
                                      KuiHttpResponseFn fn, void* user_data) {
    if (!client || !url || !fn) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        client->value.fetchAsync(str(url, len), [fn, user_data](kui::HttpClient::Response response) {
            auto* result = new (std::nothrow) KuiHttpResponse();
            if (!result) {
                /* Even allocation failure must complete the asynchronous
                 * operation so foreign runtimes can release callback state. */
                try { fn(nullptr, user_data); }
                catch (...) {}
                return;
            }
            result->value = std::move(response);
            /* The callback owns the response handle. C callbacks must not throw;
             * swallowing a foreign C++ exception is defensive, but deleting here
             * could double-free when the callback already released the handle. */
            try { fn(result, user_data); }
            catch (...) {}
        }, timeout_seconds);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_http_client_fetch_async_on_main_thread(KuiHttpClient* client, const char* url,
                                                     size_t len, double timeout_seconds,
                                                     KuiHttpResponseFn fn, void* user_data) {
    if (!client || !url || !fn) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        // Refuse up front while the platform is down, so the caller keeps user_data.
        // (C++ fetchAsyncOnMainThread is not used: it posts through
        // Platform::postToMainThread directly and so would park the result in the
        // static queue after shutdown -- see kui_c::postToMainThread.)
        if (!kui_c::platformRunning()) return KUI_ERR_NOT_SUPPORTED;
        client->value.fetchAsync(str(url, len), [fn, user_data](kui::HttpClient::Response response) {
            // HTTP worker thread. The response object is created here; ownership
            // passes to fn on the main thread, or back here if the post is refused.
            KuiHttpResponse* result = new (std::nothrow) KuiHttpResponse();
            if (result) result->value = std::move(response);
            std::shared_ptr<kui_c::MainThreadTask> task;
            try {
                task = std::make_shared<kui_c::MainThreadTask>();
                task->run = [fn, user_data, result]() { fn(result, user_data); };
                task->discard = [fn, user_data, result]() {
                    delete result;
                    fn(nullptr, user_data);
                };
            } catch (...) {
                // Cannot even build the hand-off: drop the result. fn cannot be
                // called here (wrong thread), so user_data is not released.
                delete result;
                return;
            }
            if (!kui_c::postToMainThread(task)) {
                // Platform already shutting down: the documented "never called" case.
                delete result;
            }
        }, timeout_seconds);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_http_client_cancel_all(KuiHttpClient* client) {
    if (!client) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN client->value.cancelAll(); return KUI_OK; KUI_C_END
}

KuiStatus kui_http_response_release(KuiHttpResponse* response) {
    if (!response) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN delete response; return KUI_OK; KUI_C_END
}

KuiStatus kui_http_response_get_status(KuiHttpResponse* response, int* out_status_code) {
    if (!response || !out_status_code) return KUI_ERR_INVALID_ARG;
    *out_status_code = response->value.statusCode;
    return KUI_OK;
}

KuiStatus kui_http_response_get_body(KuiHttpResponse* response, uint8_t* buf,
                                     size_t capacity, size_t* out_len) {
    if (!response) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const auto& body = response->value.body;
        return writeString(std::string_view(reinterpret_cast<const char*>(body.data()), body.size()),
                           reinterpret_cast<char*>(buf), capacity, out_len);
    KUI_C_END
}

KuiStatus kui_http_response_get_content_type(KuiHttpResponse* response,
                                             char* buf, size_t capacity,
                                             size_t* out_len) {
    if (!response) return KUI_ERR_INVALID_ARG;
    return writeString(response->value.contentType, buf, capacity, out_len);
}

KuiStatus kui_http_response_get_error(KuiHttpResponse* response, char* buf,
                                      size_t capacity, size_t* out_len) {
    if (!response) return KUI_ERR_INVALID_ARG;
    return writeString(response->value.error, buf, capacity, out_len);
}

KuiStatus kui_document_init_extensions(KuiDocument* doc, const char* directory,
                                       size_t len, size_t* out_loaded_count) {
    if (!doc || !directory) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        document(doc)->initExtensions(str(directory, len));
        auto* manager = extensions(doc);
        if (!manager) return KUI_ERR_NOT_SUPPORTED;
        if (out_loaded_count) *out_loaded_count = manager->count();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_extension_is_loaded(KuiDocument* doc, const char* tag_name,
                                           size_t len, int* out_loaded) {
    if (!doc || !tag_name || !out_loaded) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* manager = extensions(doc);
        if (!manager) return KUI_ERR_NOT_SUPPORTED;
        *out_loaded = manager->isExtensionLoaded(str(tag_name, len)) ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

// --- Section 14 (3.61.8): extension lifecycle -----------------------------------
KuiStatus kui_document_load_extension(KuiDocument* doc, const char* path, size_t len,
                                      int* out_loaded) {
    if (!doc || !path) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* manager = extensions(doc);
        if (!manager) return KUI_ERR_NOT_SUPPORTED;
        const bool loaded = manager->loadExtension(str(path, len));
        if (out_loaded) *out_loaded = loaded ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_unload_extension(KuiDocument* doc, const char* tag_name, size_t len) {
    if (!doc || !tag_name) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* manager = extensions(doc);
        if (!manager) return KUI_ERR_NOT_SUPPORTED;
        return manager->unloadExtension(str(tag_name, len)) ? KUI_OK : KUI_ERR_NOT_FOUND;
    KUI_C_END
}

KuiStatus kui_document_reload_extension(KuiDocument* doc, const char* path, size_t len,
                                        int* out_reloaded) {
    if (!doc || !path) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* manager = extensions(doc);
        if (!manager) return KUI_ERR_NOT_SUPPORTED;
        const bool reloaded = manager->reloadExtension(str(path, len));
        if (out_reloaded) *out_reloaded = reloaded ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_unload_all_extensions(KuiDocument* doc) {
    if (!doc) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* manager = extensions(doc);
        if (!manager) return KUI_ERR_NOT_SUPPORTED;
        manager->unloadAll();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_watch_extensions(KuiDocument* doc, const char* directory, size_t len) {
    if (!doc) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* manager = extensions(doc);
        if (!manager) return KUI_ERR_NOT_SUPPORTED;
        if (directory) manager->startWatching(str(directory, len));
        else manager->stopWatching();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_get_extension_state(KuiDocument* doc, size_t* out_count, int* out_watching) {
    if (!doc) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* manager = extensions(doc);
        if (!manager) return KUI_ERR_NOT_SUPPORTED;
        if (out_count) *out_count = manager->count();
        if (out_watching) *out_watching = manager->isWatching() ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_extension_invoke(KuiDocument* doc, KuiElement* target,
                                        const char* method, size_t method_len,
                                        const KuiCExtensionValue* args,
                                        size_t arg_count, size_t max_results,
                                        KuiExtensionResult** out_result) {
    if (!doc || !target || !method || !out_result || (arg_count && !args))
        return KUI_ERR_INVALID_ARG;
    *out_result = nullptr;
    KUI_C_BEGIN
        auto* manager = extensions(doc);
        if (!manager) return KUI_ERR_NOT_SUPPORTED;
        auto result = std::make_unique<KuiExtensionResult>();
        result->value = manager->invoke(element(target), str(method, method_len),
                                        extensionArgs(args, arg_count), max_results);
        *out_result = result.release();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_extension_result_release(KuiExtensionResult* result) {
    if (!result) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN delete result; return KUI_OK; KUI_C_END
}

KuiStatus kui_extension_result_get_status(KuiExtensionResult* result, int* out_status) {
    if (!result || !out_status) return KUI_ERR_INVALID_ARG;
    *out_status = static_cast<int>(result->value.status);
    return KUI_OK;
}

KuiStatus kui_extension_result_get_count(KuiExtensionResult* result, size_t* out_count) {
    if (!result || !out_count) return KUI_ERR_INVALID_ARG;
    *out_count = result->value.values.size();
    return KUI_OK;
}

KuiStatus kui_extension_result_get_value(KuiExtensionResult* result, size_t index,
                                         KuiCExtensionValue* out_value) {
    if (!out_value) return KUI_ERR_INVALID_ARG;
    const kui::ExtensionValue* value = nullptr;
    const auto status = requireExtensionValue(result, index, &value);
    if (status != KUI_OK) return status;
    out_value->type = static_cast<KuiCExtensionValueType>(value->type);
    out_value->int_value = value->intValue;
    out_value->float_value = value->floatValue;
    out_value->string_value = nullptr;
    out_value->string_len = value->stringValue.size();
    out_value->bool_value = value->boolValue ? 1 : 0;
    return KUI_OK;
}

KuiStatus kui_extension_result_get_string(KuiExtensionResult* result, size_t index,
                                          char* buf, size_t capacity, size_t* out_len) {
    const kui::ExtensionValue* value = nullptr;
    const auto status = requireExtensionValue(result, index, &value);
    if (status != KUI_OK) return status;
    if (value->type != KUI_VAL_STRING) return KUI_ERR_NOT_SUPPORTED;
    return writeString(value->stringValue, buf, capacity, out_len);
}

KuiStatus kui_extension_result_get_error(KuiExtensionResult* result, char* buf,
                                         size_t capacity, size_t* out_len) {
    if (!result) return KUI_ERR_INVALID_ARG;
    return writeString(result->value.error, buf, capacity, out_len);
}

KuiStatus kui_document_extension_get_perf_stats(KuiDocument* doc, const char* tag_name,
                                                size_t len,
                                                KuiCExtensionPerfStats* out_stats) {
    if (!doc || !tag_name || !out_stats) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* manager = extensions(doc);
        if (!manager) return KUI_ERR_NOT_SUPPORTED;
        const auto value = manager->getPerfStats(str(tag_name, len));
        out_stats->last_tick_ms = value.lastTickMs;
        out_stats->last_paint_ms = value.lastPaintMs;
        out_stats->max_tick_ms = value.maxTickMs;
        out_stats->max_paint_ms = value.maxPaintMs;
        out_stats->tick_count = value.tickCount;
        out_stats->paint_count = value.paintCount;
        out_stats->total_tick_ms = value.totalTickMs;
        out_stats->total_paint_ms = value.totalPaintMs;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_extension_reset_perf_stats(KuiDocument* doc) {
    if (!doc) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* manager = extensions(doc);
        if (!manager) return KUI_ERR_NOT_SUPPORTED;
        manager->resetPerfStats();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_define_custom_element(KuiDocument* doc, const char* tag_name,
                                             size_t len,
                                             const KuiCCustomElementCallbacks* callbacks) {
    if (!doc || !tag_name) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* registry = document(doc)->customElements();
        if (!registry) return KUI_ERR_NOT_SUPPORTED;
        const auto tag = str(tag_name, len);
        const KuiCCustomElementCallbacks cb = callbacks ? *callbacks
                                                        : KuiCCustomElementCallbacks{};
        kui::CustomElementDefinition definition;
        definition.tagName = tag;
        definition.factory = [tag](kui::Document* owner) {
            return kui::Element::create(owner, tag);
        };
        if (cb.observed_attribute_count && !cb.observed_attributes)
            return KUI_ERR_INVALID_ARG;
        definition.observedAttributes.reserve(cb.observed_attribute_count);
        for (size_t index = 0; index < cb.observed_attribute_count; ++index) {
            if (!cb.observed_attributes[index]) return KUI_ERR_INVALID_ARG;
            definition.observedAttributes.emplace_back(cb.observed_attributes[index]);
        }
        if (cb.connected) {
            definition.connectedCallback = [cb](kui::Element* native) {
                cb.connected(toHandle<KuiElement>(native), cb.user_data);
            };
        }
        if (cb.disconnected) {
            definition.disconnectedCallback = [cb](kui::Element* native) {
                cb.disconnected(toHandle<KuiElement>(native), cb.user_data);
            };
        }
        if (cb.attribute_changed) {
            definition.attributeChangedCallback = [cb](kui::Element* native,
                                                        const kui::String& name,
                                                        const kui::String& oldValue,
                                                        const kui::String& newValue) {
                cb.attribute_changed(toHandle<KuiElement>(native),
                                     name.data(), name.size(),
                                     oldValue.data(), oldValue.size(),
                                     newValue.data(), newValue.size(), cb.user_data);
            };
        }
        return registry->define(tag, std::move(definition)) ? KUI_OK : KUI_ERR_INVALID_ARG;
    KUI_C_END
}

KuiStatus kui_document_custom_element_is_defined(KuiDocument* doc, const char* tag_name,
                                                 size_t len, int* out_defined) {
    if (!doc || !tag_name || !out_defined) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* registry = document(doc)->customElements();
        if (!registry) return KUI_ERR_NOT_SUPPORTED;
        *out_defined = registry->isDefined(view(tag_name, len)) ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

// --- Section 17 (3.61.8): custom element paint, tick and input ----------------------
}  // extern "C": the helpers below are C++

struct KuiPaintContext {
    kui::DisplayList* list = nullptr;
    float width = 0;
    float height = 0;
    float dpr = 1;
    int saves = 0;  // unbalanced saves are restored after the callback
};

namespace {

kui::String textFamily(const KuiCTextStyle* style) {
    return style && style->font_family ? kui::String(style->font_family) : kui::String("sans-serif");
}
kui::f32 textSize(const KuiCTextStyle* style) {
    return style && style->font_size > 0 ? style->font_size : 14.0f;
}
kui::u16 textWeight(const KuiCTextStyle* style) {
    return style && style->font_weight ? style->font_weight : static_cast<kui::u16>(400);
}
kui::u8 textItalic(const KuiCTextStyle* style) { return style && style->italic ? 1 : 0; }

bool cornerRadii(const KuiCCornerRadii* radii, kui::CornerRadii& out) {
    if (!radii) return false;
    const float values[] = {radii->top_left_x, radii->top_left_y, radii->top_right_x,
                            radii->top_right_y, radii->bottom_right_x, radii->bottom_right_y,
                            radii->bottom_left_x, radii->bottom_left_y};
    for (float value : values) {
        if (!(value >= 0)) return false;  // negative or NaN
    }
    out.topLeft = {radii->top_left_x, radii->top_left_y};
    out.topRight = {radii->top_right_x, radii->top_right_y};
    out.bottomRight = {radii->bottom_right_x, radii->bottom_right_y};
    out.bottomLeft = {radii->bottom_left_x, radii->bottom_left_y};
    return true;
}

// Copies the C definition into native callbacks that share one user_data holder.
void applyHooks(const KuiCCustomElementDefinition& def, const kui_c::UserDataPtr& holder,
                kui::CustomElementDefinition& native) {
    if (auto fn = def.paint) {
        native.paintCallback = [fn, holder](kui::CustomElementDefinition::PaintContext& ctx) {
            KuiPaintContext paint;
            paint.list = ctx.displayList;
            paint.width = ctx.contentWidth;
            paint.height = ctx.contentHeight;
            paint.dpr = ctx.devicePixelRatio;
            paint.list->save();  // the callback's transforms, clips and opacity end with it
            fn(toHandle<KuiElement>(ctx.element), &paint, holder->data);
            for (; paint.saves > 0; --paint.saves) paint.list->restore();
            paint.list->restore();
        };
    }
    if (auto fn = def.tick) {
        native.tickCallback = [fn, holder](kui::Element* element, kui::f64 delta) {
            fn(toHandle<KuiElement>(element), delta, holder->data);
        };
    }
    if (auto fn = def.needs_tick) {
        native.needsTickCallback = [fn, holder](kui::Element* element) {
            return fn(toHandle<KuiElement>(element), holder->data) != 0;
        };
    }
    if (auto fn = def.key) {
        native.keyEventCallback = [fn, holder](kui::Element* element,
                                               const kui::CustomElementDefinition::KeyEventData& data) {
            KuiCCustomKeyEvent event{};
            event.type = static_cast<KuiCCustomKeyType>(data.eventType);
            event.key = data.key.c_str();
            event.key_len = data.key.size();
            event.code = data.code.c_str();
            event.code_len = data.code.size();
            event.modifiers = data.modifiers;
            event.is_repeat = data.isRepeat;
            return fn(toHandle<KuiElement>(element), &event, holder->data) != 0;
        };
    }
    if (auto fn = def.mouse) {
        native.mouseEventCallback = [fn, holder](kui::Element* element,
                                                 const kui::CustomElementDefinition::MouseEventData& data) {
            KuiCCustomMouseEvent event{};
            event.type = static_cast<KuiCCustomMouseType>(data.eventType);
            event.x = data.x;
            event.y = data.y;
            event.button = data.button;
            event.buttons = data.buttons;
            event.modifiers = data.modifiers;
            event.click_count = data.clickCount;
            return fn(toHandle<KuiElement>(element), &event, holder->data) != 0;
        };
    }
    if (auto fn = def.wheel) {
        native.wheelEventCallback = [fn, holder](kui::Element* element,
                                                 const kui::CustomElementDefinition::WheelEventData& data) {
            KuiCCustomWheelEvent event{};
            event.x = data.x;
            event.y = data.y;
            event.delta_x = data.deltaX;
            event.delta_y = data.deltaY;
            event.modifiers = data.modifiers;
            return fn(toHandle<KuiElement>(element), &event, holder->data) != 0;
        };
    }
    if (auto fn = def.ime) {
        native.imeEventCallback = [fn, holder](kui::Element* element,
                                               const kui::CustomElementDefinition::ImeEventData& data) {
            KuiCCustomImeEvent event{};
            event.type = static_cast<KuiCImeEventType>(data.eventType);
            event.text = data.text.c_str();
            event.text_len = data.text.size();
            event.caret_pos = data.caretPos;
            event.committed_text = data.committedText.c_str();
            event.committed_len = data.committedText.size();
            return fn(toHandle<KuiElement>(element), &event, holder->data) != 0;
        };
    }
}

}  // namespace

extern "C" {

KuiStatus kui_document_define_custom_element_ex(KuiDocument* doc, const char* tag_name, size_t len,
                                                const KuiCCustomElementDefinition* definition) {
    if (!doc || !tag_name || !definition) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* registry = document(doc)->customElements();
        if (!registry) return KUI_ERR_NOT_SUPPORTED;
        const auto tag = str(tag_name, len);
        const KuiCCustomElementCallbacks& cb = definition->callbacks;
        if (cb.observed_attribute_count && !cb.observed_attributes) return KUI_ERR_INVALID_ARG;
        kui_c::ArmOnSuccess guard(std::make_shared<kui_c::UserData>(cb.user_data, definition->release_fn));
        const kui_c::UserDataPtr holder = guard.holder;
        kui::CustomElementDefinition native;
        native.tagName = tag;
        native.factory = [tag](kui::Document* owner) { return kui::Element::create(owner, tag); };
        native.observedAttributes.reserve(cb.observed_attribute_count);
        for (size_t index = 0; index < cb.observed_attribute_count; ++index) {
            if (!cb.observed_attributes[index]) return KUI_ERR_INVALID_ARG;
            native.observedAttributes.emplace_back(cb.observed_attributes[index]);
        }
        if (auto fn = cb.connected) {
            native.connectedCallback = [fn, holder](kui::Element* element) {
                fn(toHandle<KuiElement>(element), holder->data);
            };
        }
        if (auto fn = cb.disconnected) {
            native.disconnectedCallback = [fn, holder](kui::Element* element) {
                fn(toHandle<KuiElement>(element), holder->data);
            };
        }
        if (auto fn = cb.attribute_changed) {
            native.attributeChangedCallback = [fn, holder](kui::Element* element, const kui::String& name,
                                                           const kui::String& oldValue,
                                                           const kui::String& newValue) {
                fn(toHandle<KuiElement>(element), name.data(), name.size(), oldValue.data(),
                   oldValue.size(), newValue.data(), newValue.size(), holder->data);
            };
        }
        applyHooks(*definition, holder, native);
        if (!registry->define(tag, std::move(native))) return KUI_ERR_INVALID_ARG;
        guard.commit();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_paint_get_info(KuiPaintContext* paint, float* out_width, float* out_height,
                             float* out_device_pixel_ratio) {
    if (!paint) return KUI_ERR_INVALID_ARG;
    if (out_width) *out_width = paint->width;
    if (out_height) *out_height = paint->height;
    if (out_device_pixel_ratio) *out_device_pixel_ratio = paint->dpr;
    return KUI_OK;
}

KuiStatus kui_paint_fill_rect(KuiPaintContext* paint, float x, float y, float width, float height,
                              uint32_t rgba) {
    if (!paint || !paint->list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN paint->list->fillRect({x, y, width, height}, kui::Color::fromRGBA(rgba)); return KUI_OK; KUI_C_END
}

KuiStatus kui_paint_fill_rounded_rect(KuiPaintContext* paint, float x, float y, float width,
                                      float height, const KuiCCornerRadii* radii, uint32_t rgba) {
    kui::CornerRadii native;
    if (!paint || !paint->list || !cornerRadii(radii, native)) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        paint->list->fillRoundedRect({x, y, width, height}, native, kui::Color::fromRGBA(rgba));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_paint_stroke_rounded_rect(KuiPaintContext* paint, float x, float y, float width,
                                        float height, const KuiCCornerRadii* radii, float line_width,
                                        uint32_t rgba) {
    kui::CornerRadii native;
    if (!paint || !paint->list || line_width <= 0 || !cornerRadii(radii, native))
        return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        paint->list->strokeRoundedRect({x, y, width, height}, native, kui::Color::fromRGBA(rgba),
                                       line_width);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_paint_stroke_rect(KuiPaintContext* paint, float x, float y, float width, float height,
                                float line_width, uint32_t rgba) {
    if (!paint || !paint->list || line_width <= 0) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        paint->list->strokeRect({x, y, width, height}, kui::Color::fromRGBA(rgba), line_width);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_paint_draw_text(KuiPaintContext* paint, const char* text, size_t len, float x,
                              float baseline_y, const KuiCTextStyle* style, uint32_t rgba) {
    if (!paint || !paint->list || !text) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const kui::String value = str(text, len);
        if (value.empty()) return KUI_OK;
        paint->list->drawText(value, {x, baseline_y}, textFamily(style), textSize(style),
                              textWeight(style), textItalic(style), kui::Color::fromRGBA(rgba),
                              /*maxWidth*/ 0, /*ellipsis*/ false, /*textAlign*/ 0,
                              /*lineHeightMult*/ 0, style ? style->letter_spacing : 0.0f,
                              style ? style->word_spacing : 0.0f, /*textIndent*/ 0);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_paint_draw_pixels(KuiPaintContext* paint, const uint32_t* pixels, uint32_t width,
                                uint32_t height, float x, float y, float dest_width,
                                float dest_height) {
    if (!paint || !paint->list || !pixels || width == 0 || height == 0) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        paint->list->drawPixelBuffer(pixels, width, height, {x, y, dest_width, dest_height});
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_paint_draw_image(KuiPaintContext* paint, const char* path, size_t len,
                               const KuiRect* src, const KuiRect* dest) {
    if (!paint || !paint->list || !path || !src || !dest) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const kui::String file = str(path, len);
        if (file.empty()) return KUI_ERR_INVALID_ARG;
        paint->list->drawImage(file, {src->x, src->y, src->width, src->height},
                               {dest->x, dest->y, dest->width, dest->height});
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_paint_draw_decoration_line(KuiPaintContext* paint, float x, float y, float width,
                                         float thickness, KuiCDecorationStyle style, uint32_t rgba) {
    if (!paint || !paint->list || width <= 0 || thickness <= 0 || style < KUI_C_DECORATION_SOLID ||
        style > KUI_C_DECORATION_WAVY)
        return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        paint->list->drawDecorationLine(x, y, width, thickness, static_cast<kui::u8>(style),
                                        kui::Color::fromRGBA(rgba));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_paint_save(KuiPaintContext* paint) {
    if (!paint || !paint->list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN paint->list->save(); ++paint->saves; return KUI_OK; KUI_C_END
}

KuiStatus kui_paint_restore(KuiPaintContext* paint) {
    if (!paint || !paint->list || paint->saves == 0) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN paint->list->restore(); --paint->saves; return KUI_OK; KUI_C_END
}

KuiStatus kui_paint_translate(KuiPaintContext* paint, float dx, float dy) {
    if (!paint || !paint->list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN paint->list->translate(dx, dy); return KUI_OK; KUI_C_END
}

KuiStatus kui_paint_scale(KuiPaintContext* paint, float sx, float sy) {
    if (!paint || !paint->list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN paint->list->scale(sx, sy); return KUI_OK; KUI_C_END
}

KuiStatus kui_paint_rotate(KuiPaintContext* paint, float degrees) {
    if (!paint || !paint->list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN paint->list->rotate(degrees); return KUI_OK; KUI_C_END
}

KuiStatus kui_paint_set_opacity(KuiPaintContext* paint, float opacity) {
    if (!paint || !paint->list || !(opacity >= 0 && opacity <= 1)) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN paint->list->setOpacity(opacity); return KUI_OK; KUI_C_END
}

KuiStatus kui_paint_clip_rect(KuiPaintContext* paint, float x, float y, float width, float height) {
    if (!paint || !paint->list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN paint->list->clipRect({x, y, width, height}); return KUI_OK; KUI_C_END
}

KuiStatus kui_paint_clip_rounded_rect(KuiPaintContext* paint, float x, float y, float width,
                                      float height, const KuiCCornerRadii* radii) {
    kui::CornerRadii native;
    if (!paint || !paint->list || !cornerRadii(radii, native)) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN paint->list->clipRoundedRect({x, y, width, height}, native); return KUI_OK; KUI_C_END
}

KuiStatus kui_element_measure_text(KuiElement* value, const char* text, size_t len,
                                   const KuiCTextStyle* style, KuiCTextMetrics* out_metrics) {
    if (!value || !text || !out_metrics) return KUI_ERR_INVALID_ARG;
    *out_metrics = KuiCTextMetrics{};
    KUI_C_BEGIN
        auto* owner = element(value)->ownerDocument();
        auto* backend = owner ? owner->renderBackend() : nullptr;
        if (!backend) return KUI_ERR_NOT_SUPPORTED;
        const auto family = textFamily(style);
        const auto metrics = backend->measureFontMetrics(family, textSize(style), textWeight(style),
                                                         textItalic(style));
        out_metrics->ascent = metrics.ascent;
        out_metrics->descent = metrics.descent;
        out_metrics->line_height = metrics.lineHeight;
        const kui::String value_text = str(text, len);
        if (!value_text.empty()) {
            out_metrics->width = backend->measureSubstring(value_text,
                                                           static_cast<kui::u32>(value_text.size()),
                                                           family, textSize(style), textWeight(style),
                                                           textItalic(style));
        }
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_request_repaint(KuiElement* value) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* native = element(value);
        native->markNeedsPaint();
        if (auto* owner = native->ownerDocument()) owner->scheduleRepaint();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_invalidate_rect(KuiElement* value, float x, float y, float width, float height) {
    if (!value || width <= 0 || height <= 0) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* native = element(value);
        auto* owner = native->ownerDocument();
        if (!owner) return KUI_OK;
        auto* renderer = owner->renderer();
        auto* box = native->layoutBox();
        if (renderer && box) {
            const kui::Rect content = box->absoluteContentRect();
            renderer->invalidateRect({content.x + x, content.y + y, width, height});
        } else {
            native->markNeedsPaint();
        }
        owner->scheduleRepaint();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_devtools_dispatch_ime_event(KuiDocument* doc, KuiCImeEventType type, const char* text,
                                          size_t len) {
    if (!doc || type < KUI_C_IME_START || type > KUI_C_IME_CANCEL) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        auto* dispatcher = document(doc)->eventDispatcher();
        if (!dispatcher) return KUI_ERR_NOT_FOUND;
        const kui::String value = text ? str(text, len) : kui::String();
        switch (type) {
            case KUI_C_IME_START: dispatcher->handleIMECompositionStart(); break;
            case KUI_C_IME_UPDATE: dispatcher->handleIMECompositionUpdate(value); break;
            case KUI_C_IME_COMMIT: dispatcher->handleIMECommit(value); break;
            case KUI_C_IME_CANCEL: dispatcher->handleIMECompositionEnd(); break;
        }
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_print_to_pdf(KuiDocument* doc, const char* path, size_t path_len,
                                    const KuiCPDFOptions* options, int* out_written) {
    if (!doc || !path) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::PDFOptions native;
        if (options) {
            if (options->title) native.title = options->title;
            if (options->author) native.author = options->author;
            if (options->subject) native.subject = options->subject;
            if (options->keywords) native.keywords = options->keywords;
            if (options->creator) native.creator = options->creator;
            if (options->creation_date) native.creationDate = options->creation_date;
            if (options->page_width_pt > 0) native.pageWidth_pt = options->page_width_pt;
            if (options->page_height_pt > 0) native.pageHeight_pt = options->page_height_pt;
            native.rasterizePDFFonts = options->rasterize_fonts != 0;
            native.generatePDFOutline = options->generate_outline != 0;
        }
        auto* nativeDocument = document(doc);

        // Standalone C ABI documents are intentionally usable without a
        // Window, but PDF export still needs a real RenderBackend for layout,
        // font metrics and SkPDF emission. Attach a tiny headless backend for
        // the duration of this call when the document has no renderer yet.
        // Window-owned documents keep using their existing renderer.
        std::unique_ptr<kui::RenderBackend> headlessBackend;
        std::unique_ptr<kui::Renderer> headlessRenderer;
        struct RendererRestore {
            kui::Document* document = nullptr;
            kui::Renderer* previous = nullptr;
            kui::RenderBackend* backend = nullptr;
            ~RendererRestore() {
                if (!document) return;
                document->setRenderer(previous);
                if (backend) backend->shutdown();
            }
        } restore;
        if (!nativeDocument->renderBackend()) {
            headlessBackend = kui::backend::createDefault();
            if (!headlessBackend || !headlessBackend->init(nullptr, 1, 1)) {
                return KUI_ERR_NOT_SUPPORTED;
            }
            headlessRenderer = std::make_unique<kui::Renderer>(
                headlessBackend.get(), nativeDocument);
            restore.document = nativeDocument;
            restore.previous = nativeDocument->renderer();
            restore.backend = headlessBackend.get();
            nativeDocument->setRenderer(headlessRenderer.get());
        }

        const bool written = nativeDocument->printToPDF(str(path, path_len), native);
        if (out_written) *out_written = written ? 1 : 0;
        return written ? KUI_OK : KUI_ERR_IO;
    KUI_C_END
}

}  // extern "C"
