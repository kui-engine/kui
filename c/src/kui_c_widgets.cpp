// =============================================================================
// kui_c_widgets.cpp — section 12 of kui_c.h: popups (tooltip / context menu),
// the <input> date / time / colour picker and the document's AudioManager
// (3.61.8).
// =============================================================================
#include "kui_c_internal.h"

#include <kui/core/color.h>
#include <kui/core/rect.h>
#include <kui/dom/document.h>
#include <kui/dom/element.h>
#include <kui/dom/html_input_element.h>
#include <kui/dom/html_form_associated_elements.h>
#include <kui/dom/html_meter_element.h>
#include <kui/dom/html_progress_element.h>
#include <kui/dom/html_select_element.h>
#include <kui/dom/html_textarea_element.h>
#include <kui/dom/input_picker.h>
#include <kui/event/event_dispatcher.h>
#include <kui/media/audio_manager.h>
#include <kui/popup/popup_manager.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using kui_c::fromHandle;
using kui_c::str;
using kui_c::toHandle;

namespace {

inline kui::Document* doc(KuiDocument* value) { return fromHandle<kui::Document>(value); }
inline kui::Element* elem(KuiElement* value) { return fromHandle<kui::Element>(value); }

kui::Color unpack(uint32_t rgba) {
    return kui::Color(static_cast<kui::u8>(rgba >> 24), static_cast<kui::u8>(rgba >> 16),
                      static_cast<kui::u8>(rgba >> 8), static_cast<kui::u8>(rgba));
}
uint32_t pack(const kui::Color& color) {
    return (uint32_t(color.r) << 24) | (uint32_t(color.g) << 16) | (uint32_t(color.b) << 8) |
           uint32_t(color.a);
}

bool validType(KuiCPopupType type) {
    return type == KUI_C_POPUP_TOOLTIP || type == KUI_C_POPUP_CONTEXT_MENU || type == KUI_C_POPUP_CUSTOM;
}

// Shared by every item of one context menu; release_fn runs when the last item
// (and so the menu) is gone.
struct MenuCallback {
    KuiMenuActivateFn fn = nullptr;
    void* userData = nullptr;
    KuiTaskFn release = nullptr;
    ~MenuCallback() {
        if (release) release(userData);
    }
};

bool validGroup(int group) { return group >= 0 && group < static_cast<int>(kui::AudioGroup::_Count); }

}  // namespace

extern "C" {

// --- Popups --------------------------------------------------------------------
KuiStatus kui_document_show_tooltip(KuiDocument* d, const char* text, size_t len,
                                    const KuiRect* anchor, KuiCPopupPosition position,
                                    uint32_t* out_id) {
    KUI_C_BEGIN
    if (!d || !text || !anchor || position < KUI_C_POPUP_TOP || position > KUI_C_POPUP_RIGHT) {
        return KUI_ERR_INVALID_ARG;
    }
    const kui::PopupId id = doc(d)->popupManager().showTooltip(
        str(text, len), kui::Rect(anchor->x, anchor->y, anchor->width, anchor->height),
        static_cast<kui::PopupPosition>(position));
    if (out_id) *out_id = id;
    doc(d)->scheduleRepaint();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_show_context_menu(KuiDocument* d, float x, float y,
                                         const KuiCMenuItem* items, size_t item_count,
                                         KuiMenuActivateFn fn, void* user_data,
                                         KuiTaskFn release_fn, uint32_t* out_id) {
    KUI_C_BEGIN
    if (!d || !fn || !item_count || !items) return KUI_ERR_INVALID_ARG;
    std::vector<kui::MenuItem> menu;
    menu.reserve(item_count);
    for (size_t index = 0; index < item_count; ++index) {
        if (!items[index].separator && !items[index].text) return KUI_ERR_INVALID_ARG;
    }
    auto callback = std::make_shared<MenuCallback>();
    callback->fn = fn;
    callback->userData = user_data;
    for (size_t index = 0; index < item_count; ++index) {
        const KuiCMenuItem& in = items[index];
        kui::MenuItem item;
        item.separator = in.separator != 0;
        item.disabled = in.disabled != 0;
        if (!item.separator) {
            item.text = str(in.text, in.text_len);
            const uint32_t id = in.id;
            item.onClick = [callback, id]() {
                std::shared_ptr<MenuCallback> keep = callback;  // release waits for fn
                keep->fn(id, keep->userData);
            };
        }
        menu.push_back(std::move(item));
    }
    const kui::PopupId id = doc(d)->popupManager().showContextMenu(x, y, std::move(menu));
    callback->release = release_fn;  // armed only once the menu was handed over
    if (out_id) *out_id = id;
    doc(d)->scheduleRepaint();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_dismiss_popup(KuiDocument* d, uint32_t id) {
    KUI_C_BEGIN
    if (!d) return KUI_ERR_INVALID_ARG;
    doc(d)->popupManager().dismiss(id);
    doc(d)->scheduleRepaint();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_dismiss_all_popups(KuiDocument* d) {
    KUI_C_BEGIN
    if (!d) return KUI_ERR_INVALID_ARG;
    doc(d)->popupManager().dismissAll();
    doc(d)->scheduleRepaint();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_dismiss_popups_of_type(KuiDocument* d, KuiCPopupType type) {
    KUI_C_BEGIN
    if (!d || !validType(type)) return KUI_ERR_INVALID_ARG;
    doc(d)->popupManager().dismissByType(static_cast<kui::PopupType>(type));
    doc(d)->scheduleRepaint();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_has_popups(KuiDocument* d, int* out_any) {
    KUI_C_BEGIN
    if (!d || !out_any) return KUI_ERR_INVALID_ARG;
    *out_any = doc(d)->popupManager().hasActivePopups() ? 1 : 0;
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_has_popup_of_type(KuiDocument* d, KuiCPopupType type, int* out_has) {
    KUI_C_BEGIN
    if (!d || !out_has || !validType(type)) return KUI_ERR_INVALID_ARG;
    *out_has = doc(d)->popupManager().hasPopupOfType(static_cast<kui::PopupType>(type)) ? 1 : 0;
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_set_tooltip_delay(KuiDocument* d, double ms) {
    KUI_C_BEGIN
    if (!d || !(ms >= 0.0)) return KUI_ERR_INVALID_ARG;
    doc(d)->popupManager().setTooltipDelay(ms);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_get_tooltip_delay(KuiDocument* d, double* out_ms) {
    KUI_C_BEGIN
    if (!d || !out_ms) return KUI_ERR_INVALID_ARG;
    *out_ms = doc(d)->popupManager().tooltipDelay();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_set_tooltip_theme(KuiDocument* d, const KuiCTooltipTheme* theme) {
    KUI_C_BEGIN
    if (!d || !theme || !(theme->radius >= 0.0f)) return KUI_ERR_INVALID_ARG;
    kui::TooltipTheme native;
    native.bg = unpack(theme->background);
    native.fg = unpack(theme->foreground);
    native.border = unpack(theme->border);
    native.radius = theme->radius;
    doc(d)->popupManager().setTooltipTheme(native);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_get_tooltip_theme(KuiDocument* d, KuiCTooltipTheme* out_theme) {
    KUI_C_BEGIN
    if (!d || !out_theme) return KUI_ERR_INVALID_ARG;
    const kui::TooltipTheme& native = doc(d)->popupManager().tooltipTheme();
    out_theme->background = pack(native.bg);
    out_theme->foreground = pack(native.fg);
    out_theme->border = pack(native.border);
    out_theme->radius = native.radius;
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_set_popup_mode(KuiDocument* d, KuiCPopupMode mode) {
    KUI_C_BEGIN
    if (!d || (mode != KUI_C_POPUP_MODE_OVERLAY && mode != KUI_C_POPUP_MODE_NATIVE_WINDOW)) {
        return KUI_ERR_INVALID_ARG;
    }
    doc(d)->popupManager().setPopupMode(static_cast<kui::PopupMode>(mode));
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_set_default_context_menu_enabled(KuiDocument* d, int enabled) {
    KUI_C_BEGIN
    if (!d) return KUI_ERR_INVALID_ARG;
    auto* dispatcher = doc(d)->eventDispatcher();
    if (!dispatcher) return KUI_ERR_NOT_FOUND;
    dispatcher->setDefaultContextMenuEnabled(enabled != 0);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_get_default_context_menu_enabled(KuiDocument* d, int* out_enabled) {
    KUI_C_BEGIN
    if (!d || !out_enabled) return KUI_ERR_INVALID_ARG;
    auto* dispatcher = doc(d)->eventDispatcher();
    if (!dispatcher) return KUI_ERR_NOT_FOUND;
    *out_enabled = dispatcher->isDefaultContextMenuEnabled() ? 1 : 0;
    return KUI_OK;
    KUI_C_END
}

// --- Form control state -------------------------------------------------------
// 与 Lua 的 element.value / checked / selectedIndex 同一套按元素类型的分派，读写的是控件当前状态
// （用户键入 / 选择的），不是内容属性。写入不派发 input / change 事件（与给 DOM 属性赋值一致）。
namespace {
// 不返回类类型：这一段在 extern "C" 区里（C4190）。
struct NumberText {
    char text[40];
};
void formatNumber(double value, NumberText* out) { std::snprintf(out->text, sizeof out->text, "%.15g", value); }
bool parseNumber(const char* text, double* out) {
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || *end != '\0') return false;
    *out = value;
    return true;
}
}  // namespace

KuiStatus kui_element_get_value(KuiElement* el, char* buf, size_t buf_capacity, size_t* out_len) {
    KUI_C_BEGIN
    if (!el) return KUI_ERR_INVALID_ARG;
    kui::Element* e = elem(el);
    if (auto* input = dynamic_cast<kui::HTMLInputElement*>(e))
        return kui_c::writeString(input->value(), buf, buf_capacity, out_len);
    if (auto* select = dynamic_cast<kui::HTMLSelectElement*>(e))
        return kui_c::writeString(select->value(), buf, buf_capacity, out_len);
    if (auto* area = dynamic_cast<kui::HTMLTextAreaElement*>(e))
        return kui_c::writeString(area->value(), buf, buf_capacity, out_len);
    if (auto* output = dynamic_cast<kui::HTMLOutputElement*>(e))
        return kui_c::writeString(output->value(), buf, buf_capacity, out_len);
    NumberText number;
    if (auto* meter = dynamic_cast<kui::HTMLMeterElement*>(e)) {
        formatNumber(meter->value(), &number);
        return kui_c::writeString(number.text, buf, buf_capacity, out_len);
    }
    if (auto* progress = dynamic_cast<kui::HTMLProgressElement*>(e)) {
        formatNumber(progress->value(), &number);
        return kui_c::writeString(number.text, buf, buf_capacity, out_len);
    }
    return KUI_ERR_NOT_SUPPORTED;
    KUI_C_END
}

KuiStatus kui_element_set_value(KuiElement* el, const char* value, size_t value_len) {
    KUI_C_BEGIN
    if (!el || (!value && value_len != 0)) return KUI_ERR_INVALID_ARG;
    kui::Element* e = elem(el);
    const std::string text = str(value, value_len);
    if (auto* input = dynamic_cast<kui::HTMLInputElement*>(e)) { input->setValue(text); return KUI_OK; }
    if (auto* select = dynamic_cast<kui::HTMLSelectElement*>(e)) { select->setValue(text); return KUI_OK; }
    if (auto* area = dynamic_cast<kui::HTMLTextAreaElement*>(e)) { area->setValue(text); return KUI_OK; }
    if (auto* output = dynamic_cast<kui::HTMLOutputElement*>(e)) { output->setValue(text); return KUI_OK; }
    double number = 0;
    if (auto* meter = dynamic_cast<kui::HTMLMeterElement*>(e)) {
        if (!parseNumber(text.c_str(), &number)) return KUI_ERR_INVALID_ARG;
        meter->setValue(number);
        return KUI_OK;
    }
    if (auto* progress = dynamic_cast<kui::HTMLProgressElement*>(e)) {
        if (!parseNumber(text.c_str(), &number)) return KUI_ERR_INVALID_ARG;
        progress->setValue(number);
        return KUI_OK;
    }
    return KUI_ERR_NOT_SUPPORTED;
    KUI_C_END
}

KuiStatus kui_element_get_checked(KuiElement* el, int* out_checked) {
    KUI_C_BEGIN
    if (!el || !out_checked) return KUI_ERR_INVALID_ARG;
    auto* input = dynamic_cast<kui::HTMLInputElement*>(elem(el));
    if (!input) return KUI_ERR_NOT_SUPPORTED;
    *out_checked = input->isChecked() ? 1 : 0;
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_set_checked(KuiElement* el, int checked) {
    KUI_C_BEGIN
    if (!el) return KUI_ERR_INVALID_ARG;
    auto* input = dynamic_cast<kui::HTMLInputElement*>(elem(el));
    if (!input) return KUI_ERR_NOT_SUPPORTED;
    input->setChecked(checked != 0);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_get_selected_index(KuiElement* el, int32_t* out_index) {
    KUI_C_BEGIN
    if (!el || !out_index) return KUI_ERR_INVALID_ARG;
    auto* select = dynamic_cast<kui::HTMLSelectElement*>(elem(el));
    if (!select) return KUI_ERR_NOT_SUPPORTED;
    *out_index = select->selectedIndex();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_element_set_selected_index(KuiElement* el, int32_t index) {
    KUI_C_BEGIN
    if (!el) return KUI_ERR_INVALID_ARG;
    auto* select = dynamic_cast<kui::HTMLSelectElement*>(elem(el));
    if (!select) return KUI_ERR_NOT_SUPPORTED;
    select->setSelectedIndexSilently(index);
    return KUI_OK;
    KUI_C_END
}

// --- Input picker -------------------------------------------------------------
KuiStatus kui_element_open_input_picker(KuiElement* el, KuiCPickerMode mode) {
    KUI_C_BEGIN
    if (!el || mode < KUI_C_PICKER_NONE || mode > KUI_C_PICKER_COLOR) return KUI_ERR_INVALID_ARG;
    auto* input = dynamic_cast<kui::HTMLInputElement*>(elem(el));
    kui::Document* owner = elem(el)->ownerDocument();
    if (!input || !owner) return KUI_ERR_INVALID_ARG;
    kui::InputPicker& picker = owner->inputPicker();
    if (!picker.isEnabled()) return KUI_ERR_NOT_SUPPORTED;
    switch (mode) {
    case KUI_C_PICKER_NONE: picker.open(input); break;
    case KUI_C_PICKER_DATE: picker.openDate(input); break;
    case KUI_C_PICKER_TIME: picker.openTime(input); break;
    case KUI_C_PICKER_DATETIME_LOCAL: picker.openDateTimeLocal(input); break;
    case KUI_C_PICKER_COLOR: picker.openColor(input); break;
    }
    if (!picker.isOpen() || picker.targetInput() != input) return KUI_ERR_NOT_SUPPORTED;
    owner->scheduleRepaint();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_close_input_picker(KuiDocument* d) {
    KUI_C_BEGIN
    if (!d) return KUI_ERR_INVALID_ARG;
    doc(d)->inputPicker().close();
    doc(d)->scheduleRepaint();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_commit_input_picker(KuiDocument* d) {
    KUI_C_BEGIN
    if (!d) return KUI_ERR_INVALID_ARG;
    doc(d)->inputPicker().commit();
    doc(d)->scheduleRepaint();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_get_input_picker_state(KuiDocument* d, KuiCInputPickerState* out_state) {
    KUI_C_BEGIN
    if (!d || !out_state) return KUI_ERR_INVALID_ARG;
    const kui::InputPicker& picker = doc(d)->inputPicker();
    out_state->open = picker.isOpen() ? 1 : 0;
    out_state->enabled = picker.isEnabled() ? 1 : 0;
    out_state->mode = static_cast<KuiCPickerMode>(picker.mode());
    out_state->visible_year = picker.visibleYear();
    out_state->visible_month = picker.visibleMonth();
    out_state->color_h = picker.colorH();
    out_state->color_s = picker.colorS();
    out_state->color_v = picker.colorV();
    out_state->hour = picker.timeHour();
    out_state->minute = picker.timeMinute();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_get_input_picker_target(KuiDocument* d, KuiElement** out) {
    KUI_C_BEGIN
    if (!d || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    kui::HTMLInputElement* input = doc(d)->inputPicker().targetInput();
    if (!input) return KUI_ERR_NOT_FOUND;
    kui::Element* element = input;
    element->ref();
    *out = toHandle<KuiElement>(element);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_set_input_picker_enabled(KuiDocument* d, int enabled) {
    KUI_C_BEGIN
    if (!d) return KUI_ERR_INVALID_ARG;
    doc(d)->inputPicker().setEnabled(enabled != 0);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_set_input_picker_month(KuiDocument* d, int year, int month) {
    KUI_C_BEGIN
    if (!d || month < 1 || month > 12) return KUI_ERR_INVALID_ARG;
    doc(d)->inputPicker().setVisibleYearMonth(year, month);
    doc(d)->scheduleRepaint();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_set_input_picker_color_hsv(KuiDocument* d, float h, float s, float v) {
    KUI_C_BEGIN
    if (!d || !(h >= 0.0f && h <= 360.0f) || !(s >= 0.0f && s <= 1.0f) || !(v >= 0.0f && v <= 1.0f)) {
        return KUI_ERR_INVALID_ARG;
    }
    doc(d)->inputPicker().setColorHsv(h, s, v);
    doc(d)->scheduleRepaint();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_set_input_picker_time(KuiDocument* d, int hour, int minute) {
    KUI_C_BEGIN
    if (!d || hour < 0 || hour > 23 || minute < 0 || minute > 59) return KUI_ERR_INVALID_ARG;
    doc(d)->inputPicker().setTime(hour, minute);
    doc(d)->scheduleRepaint();
    return KUI_OK;
    KUI_C_END
}

// --- Game audio ---------------------------------------------------------------
KuiStatus kui_document_audio_is_available(KuiDocument* d, int* out_available) {
    KUI_C_BEGIN
    if (!d || !out_available) return KUI_ERR_INVALID_ARG;
    *out_available = doc(d)->audioManager().isAvailable() ? 1 : 0;
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_audio_play(KuiDocument* d, const char* path, size_t len,
                                  const KuiCAudioPlayOptions* options, uint32_t* out_handle) {
    KUI_C_BEGIN
    if (!d || !path || !out_handle) return KUI_ERR_INVALID_ARG;
    *out_handle = 0;
    kui::PlayOptions native;
    if (options) {
        if (!validGroup(options->group)) return KUI_ERR_INVALID_ARG;
        native.group = static_cast<kui::AudioGroup>(options->group);
        native.volume = options->volume;
        native.pan = options->pan;
        native.pitch = options->pitch;
        native.loop = options->loop != 0;
        native.preloadOnly = options->preload_only != 0;
    }
    kui::AudioManager& audio = doc(d)->audioManager();
    if (!audio.isAvailable()) return KUI_ERR_NOT_SUPPORTED;
    const kui::SoundHandle handle = audio.play(str(path, len), native);
    if (handle == kui::kInvalidSoundHandle) return KUI_ERR_IO;
    *out_handle = handle;
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_audio_stop(KuiDocument* d, uint32_t handle) {
    KUI_C_BEGIN
    if (!d) return KUI_ERR_INVALID_ARG;
    doc(d)->audioManager().stop(handle);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_audio_set_paused(KuiDocument* d, uint32_t handle, int paused) {
    KUI_C_BEGIN
    if (!d) return KUI_ERR_INVALID_ARG;
    if (paused) doc(d)->audioManager().pause(handle);
    else doc(d)->audioManager().resume(handle);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_audio_set_volume(KuiDocument* d, uint32_t handle, float volume) {
    KUI_C_BEGIN
    if (!d) return KUI_ERR_INVALID_ARG;
    doc(d)->audioManager().setVolume(handle, volume);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_audio_set_pan(KuiDocument* d, uint32_t handle, float pan) {
    KUI_C_BEGIN
    if (!d) return KUI_ERR_INVALID_ARG;
    doc(d)->audioManager().setPan(handle, pan);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_audio_set_pitch(KuiDocument* d, uint32_t handle, float pitch) {
    KUI_C_BEGIN
    if (!d) return KUI_ERR_INVALID_ARG;
    doc(d)->audioManager().setPitch(handle, pitch);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_audio_seek(KuiDocument* d, uint32_t handle, double seconds) {
    KUI_C_BEGIN
    if (!d || !(seconds >= 0.0)) return KUI_ERR_INVALID_ARG;
    doc(d)->audioManager().seek(handle, seconds);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_audio_get_info(KuiDocument* d, uint32_t handle, KuiCSoundInfo* out_info) {
    KUI_C_BEGIN
    if (!d || !out_info) return KUI_ERR_INVALID_ARG;
    const kui::SoundInfo info = doc(d)->audioManager().info(handle);
    if (handle == kui::kInvalidSoundHandle || info.handle == kui::kInvalidSoundHandle) {
        return KUI_ERR_NOT_FOUND;
    }
    out_info->group = static_cast<KuiCAudioGroup>(info.group);
    out_info->volume = info.volume;
    out_info->pan = info.pan;
    out_info->pitch = info.pitch;
    out_info->playing = info.playing ? 1 : 0;
    out_info->looping = info.looping ? 1 : 0;
    out_info->current_time = info.currentTime;
    out_info->duration = info.duration;
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_audio_set_group(KuiDocument* d, KuiCAudioGroup group, float volume, int muted) {
    KUI_C_BEGIN
    if (!d || !validGroup(group) || !(volume >= 0.0f && volume <= 1.0f)) return KUI_ERR_INVALID_ARG;
    kui::AudioManager& audio = doc(d)->audioManager();
    audio.setGroupVolume(static_cast<kui::AudioGroup>(group), volume);
    audio.setGroupMuted(static_cast<kui::AudioGroup>(group), muted != 0);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_audio_get_group(KuiDocument* d, KuiCAudioGroup group, float* out_volume,
                                       int* out_muted) {
    KUI_C_BEGIN
    if (!d || !validGroup(group)) return KUI_ERR_INVALID_ARG;
    const kui::AudioManager& audio = doc(d)->audioManager();
    if (out_volume) *out_volume = audio.groupVolume(static_cast<kui::AudioGroup>(group));
    if (out_muted) *out_muted = audio.isGroupMuted(static_cast<kui::AudioGroup>(group)) ? 1 : 0;
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_audio_stop_all(KuiDocument* d, int group) {
    KUI_C_BEGIN
    if (!d || (group != -1 && !validGroup(group))) return KUI_ERR_INVALID_ARG;
    kui::AudioManager& audio = doc(d)->audioManager();
    if (group < 0) audio.stopAll();
    else audio.stopAll(static_cast<kui::AudioGroup>(group));
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_audio_set_all_paused(KuiDocument* d, int group, int paused) {
    KUI_C_BEGIN
    if (!d || (group != -1 && !validGroup(group))) return KUI_ERR_INVALID_ARG;
    kui::AudioManager& audio = doc(d)->audioManager();
    if (group < 0) {
        if (paused) audio.pauseAll();
        else audio.resumeAll();
    } else {
        const auto g = static_cast<kui::AudioGroup>(group);
        if (paused) audio.pauseAll(g);
        else audio.resumeAll(g);
    }
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_audio_preload(KuiDocument* d, const char* path, size_t len, uint32_t count,
                                     uint32_t* out_loaded) {
    KUI_C_BEGIN
    if (!d || !path || count == 0) return KUI_ERR_INVALID_ARG;
    const kui::u32 loaded = doc(d)->audioManager().preload(str(path, len), count);
    if (out_loaded) *out_loaded = loaded;
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_audio_unload(KuiDocument* d, const char* path, size_t len) {
    KUI_C_BEGIN
    if (!d) return KUI_ERR_INVALID_ARG;
    if (path) doc(d)->audioManager().unload(str(path, len));
    else doc(d)->audioManager().unloadAll();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_audio_get_counts(KuiDocument* d, int group, uint32_t* out_active,
                                        uint32_t* out_pooled) {
    KUI_C_BEGIN
    if (!d || (group != -1 && !validGroup(group))) return KUI_ERR_INVALID_ARG;
    const kui::AudioManager& audio = doc(d)->audioManager();
    if (out_active) {
        *out_active = group < 0 ? audio.activeCount() : audio.activeCount(static_cast<kui::AudioGroup>(group));
    }
    if (out_pooled) *out_pooled = audio.pooledCount();
    return KUI_OK;
    KUI_C_END
}

}  // extern "C"
