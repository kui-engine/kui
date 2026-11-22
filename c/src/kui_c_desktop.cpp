// =============================================================================
// kui_c_desktop.cpp — section 10 of kui_c.h: native menu bar, tray icon,
// keyboard shortcuts, toasts and screenshots (3.61.8).
//
// Menus: the native NativeMenuBar / TrayIcon address items by internal command
// ids whose assignment order is an implementation detail. The C layer keeps
// its own copy of each window's menu definition (with the caller's item ids),
// and set_checked / set_enabled edit that copy and rebuild the native menu --
// rebuilding is cheap and needs no knowledge of native ids.
// =============================================================================
#include "kui_c_internal.h"

#include <kui/dom/document.h>
#include <kui/dom/toast_manager.h>
#include <kui/platform/menu_bar.h>
#include <kui/platform/shortcut_manager.h>
#include <kui/platform/tray_icon.h>
#include <kui/platform/window.h>
#include <kui/render/renderer.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using kui_c::fromHandle;
using kui_c::str;

namespace {

inline kui::Window* win(KuiWindow* value) { return fromHandle<kui::Window>(value); }
inline kui::Document* doc(KuiDocument* value) { return fromHandle<kui::Document>(value); }

struct MenuItem {
    std::string text;
    std::string shortcut;
    uint32_t id = 0;
    bool separator = false;
    bool disabled = false;
    bool checkable = false;
    bool checked = false;
    std::vector<MenuItem> children;
};

struct Menu {
    std::string text;
    std::vector<MenuItem> items;
};

struct MenuState {
    std::vector<Menu> menus;
    KuiMenuActivateFn fn = nullptr;
    void* userData = nullptr;
};

// Per-window C-side state. Native callbacks capture a pointer to it, which stays
// valid until kui_window_release (see kui_c::forgetWindow).
struct WindowExtras {
    MenuState menuBar;
    MenuState trayMenu;
    KuiTrayEventFn trayFn = nullptr;
    void* trayUserData = nullptr;
};

std::unordered_map<kui::Window*, std::unique_ptr<WindowExtras>>& extrasMap() {
    static std::unordered_map<kui::Window*, std::unique_ptr<WindowExtras>> map;
    return map;
}

WindowExtras& extras(kui::Window* window) {
    auto& slot = extrasMap()[window];
    if (!slot) slot = std::make_unique<WindowExtras>();
    return *slot;
}

bool copyItems(const KuiCMenuItem* items, size_t count, std::vector<MenuItem>& out, int depth) {
    if (count && !items) return false;
    if (depth > 16) return false;  // a malformed (cyclic) children pointer
    out.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        const KuiCMenuItem& in = items[index];
        MenuItem item;
        item.separator = in.separator != 0;
        if (!item.separator) {
            if (!in.text) return false;
            item.text = str(in.text, in.text_len);
            if (in.shortcut_label) item.shortcut = str(in.shortcut_label, in.shortcut_label_len);
        }
        item.id = in.id;
        item.disabled = in.disabled != 0;
        item.checkable = in.checkable != 0;
        item.checked = in.checked != 0;
        if (!copyItems(in.children, in.child_count, item.children, depth + 1)) return false;
        out.push_back(std::move(item));
    }
    return true;
}

std::vector<kui::MenuItemDef> toDefs(const std::vector<MenuItem>& items, MenuState* state) {
    std::vector<kui::MenuItemDef> defs;
    defs.reserve(items.size());
    for (const MenuItem& item : items) {
        if (item.separator) {
            defs.push_back(kui::MenuItemDef::separator());
            continue;
        }
        kui::MenuItemDef def;
        def.text = item.text;
        def.shortcutLabel = item.shortcut;
        def.disabled = item.disabled;
        def.checkable = item.checkable;
        def.checked = item.checked;
        def.children = toDefs(item.children, state);
        if (def.children.empty()) {
            const uint32_t id = item.id;
            def.onClick = [state, id]() {
                if (state->fn) state->fn(id, state->userData);
            };
        }
        defs.push_back(std::move(def));
    }
    return defs;
}

kui::MenuBarDef toMenuBar(MenuState& state) {
    kui::MenuBarDef def;
    for (const Menu& menu : state.menus) def.addMenu(menu.text, toDefs(menu.items, &state));
    return def;
}

MenuItem* findItem(std::vector<MenuItem>& items, uint32_t id) {
    for (MenuItem& item : items) {
        if (!item.separator && item.children.empty() && item.id == id) return &item;
        if (MenuItem* nested = findItem(item.children, id)) return nested;
    }
    return nullptr;
}

MenuItem* findItem(MenuState& state, uint32_t id) {
    for (Menu& menu : state.menus)
        if (MenuItem* item = findItem(menu.items, id)) return item;
    return nullptr;
}

void applyMenuBar(kui::Window* window, WindowExtras& state) {
    if (state.menuBar.menus.empty())
        window->menuBar().destroy();
    else
        window->setMenuBar(toMenuBar(state.menuBar));
}

void applyTrayMenu(kui::Window* window, WindowExtras& state) {
    window->trayIcon().setContextMenu(toMenuBar(state.trayMenu));
}

void fireTray(WindowExtras* state, KuiCTrayEvent event) {
    if (state->trayFn) state->trayFn(event, state->trayUserData);
}

}  // namespace

void kui_c::forgetWindow(void* window) {
    extrasMap().erase(static_cast<kui::Window*>(window));
}

extern "C" {

// --- Menu bar ------------------------------------------------------------------

KuiStatus kui_window_set_menu_bar(KuiWindow* w, const KuiCMenu* menus, size_t menu_count,
                                  KuiMenuActivateFn fn, void* user_data) {
    if (!w || (menu_count && !menus)) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        std::vector<Menu> copy;
        copy.reserve(menu_count);
        for (size_t index = 0; index < menu_count; ++index) {
            Menu menu;
            if (menus[index].text) menu.text = str(menus[index].text, menus[index].text_len);
            if (!copyItems(menus[index].items, menus[index].item_count, menu.items, 0))
                return KUI_ERR_INVALID_ARG;
            copy.push_back(std::move(menu));
        }
        WindowExtras& state = extras(win(w));
        state.menuBar.menus = std::move(copy);
        state.menuBar.fn = fn;
        state.menuBar.userData = user_data;
        applyMenuBar(win(w), state);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_clear_menu_bar(KuiWindow* w) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        WindowExtras& state = extras(win(w));
        state.menuBar = MenuState{};
        win(w)->menuBar().destroy();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_menu_set_checked(KuiWindow* w, uint32_t item_id, int checked) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        WindowExtras& state = extras(win(w));
        MenuItem* item = findItem(state.menuBar, item_id);
        if (!item) return KUI_ERR_NOT_FOUND;
        item->checked = checked != 0;
        applyMenuBar(win(w), state);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_menu_set_enabled(KuiWindow* w, uint32_t item_id, int enabled) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        WindowExtras& state = extras(win(w));
        MenuItem* item = findItem(state.menuBar, item_id);
        if (!item) return KUI_ERR_NOT_FOUND;
        item->disabled = enabled == 0;
        applyMenuBar(win(w), state);
        return KUI_OK;
    KUI_C_END
}

// --- Tray icon -----------------------------------------------------------------

KuiStatus kui_window_tray_create(KuiWindow* w, const char* icon_path, size_t icon_path_len,
                                 const char* tooltip, size_t tooltip_len, int visible) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::TrayIconConfig config;
        if (icon_path) config.iconPath = str(icon_path, icon_path_len);
        if (tooltip) config.tooltip = str(tooltip, tooltip_len);
        config.visible = visible != 0;
        kui::TrayIcon& tray = win(w)->trayIcon();
        if (!tray.create(win(w), config)) return KUI_ERR_NOT_SUPPORTED;
        WindowExtras* state = &extras(win(w));
        tray.onClick([state]() { fireTray(state, KUI_C_TRAY_CLICK); });
        tray.onDoubleClick([state]() { fireTray(state, KUI_C_TRAY_DOUBLE_CLICK); });
        tray.onRightClick([state]() { fireTray(state, KUI_C_TRAY_RIGHT_CLICK); });
        tray.onBalloonClick([state]() { fireTray(state, KUI_C_TRAY_BALLOON_CLICK); });
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_tray_destroy(KuiWindow* w) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->trayIcon().destroy();
        WindowExtras& state = extras(win(w));
        state.trayMenu = MenuState{};
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_tray_is_active(KuiWindow* w, int* out_active) {
    if (!w || !out_active) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_active = win(w)->trayIcon().isActive() ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_tray_set_visible(KuiWindow* w, int visible) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->trayIcon().setVisible(visible != 0);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_tray_is_visible(KuiWindow* w, int* out_visible) {
    if (!w || !out_visible) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_visible = win(w)->trayIcon().isVisible() ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_tray_set_tooltip(KuiWindow* w, const char* tooltip, size_t len) {
    if (!w || !tooltip) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->trayIcon().setTooltip(str(tooltip, len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_tray_set_icon(KuiWindow* w, const char* icon_path, size_t len) {
    if (!w || !icon_path) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->trayIcon().setIcon(str(icon_path, len));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_tray_set_context_menu(KuiWindow* w, const KuiCMenuItem* items,
                                           size_t item_count, KuiMenuActivateFn fn,
                                           void* user_data) {
    if (!w || (item_count && !items)) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        Menu menu;
        if (!copyItems(items, item_count, menu.items, 0)) return KUI_ERR_INVALID_ARG;
        WindowExtras& state = extras(win(w));
        state.trayMenu.menus.clear();
        state.trayMenu.menus.push_back(std::move(menu));
        state.trayMenu.fn = fn;
        state.trayMenu.userData = user_data;
        applyTrayMenu(win(w), state);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_tray_popup_context_menu(KuiWindow* w) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        return win(w)->trayIcon().popUpContextMenu() ? KUI_OK : KUI_ERR_NOT_SUPPORTED;
    KUI_C_END
}

KuiStatus kui_window_tray_show_balloon(KuiWindow* w, const char* title, size_t title_len,
                                       const char* message, size_t message_len, int icon_type,
                                       int duration_ms) {
    if (!w || !message || icon_type < 0 || icon_type > 3) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->trayIcon().showBalloon(title ? str(title, title_len) : kui::String(),
                                       str(message, message_len), icon_type, duration_ms);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_tray_set_event_callback(KuiWindow* w, KuiTrayEventFn fn, void* user_data) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        WindowExtras& state = extras(win(w));
        state.trayFn = fn;
        state.trayUserData = fn ? user_data : nullptr;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_tray_simulate_event(KuiWindow* w, KuiCTrayEvent event) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::TrayIcon& tray = win(w)->trayIcon();
        switch (event) {
            case KUI_C_TRAY_CLICK: tray.simulateClick(); return KUI_OK;
            case KUI_C_TRAY_DOUBLE_CLICK: tray.simulateDoubleClick(); return KUI_OK;
            case KUI_C_TRAY_RIGHT_CLICK: tray.simulateRightClick(); return KUI_OK;
            case KUI_C_TRAY_BALLOON_CLICK: tray.simulateBalloonClick(); return KUI_OK;
        }
        return KUI_ERR_INVALID_ARG;
    KUI_C_END
}

// --- Shortcuts -----------------------------------------------------------------

KuiStatus kui_window_bind_shortcut(KuiWindow* w, const char* shortcut, size_t len,
                                   KuiTaskFn fn, void* user_data, uint32_t* out_id) {
    if (!w || !shortcut || !fn || !out_id) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const kui::ShortcutId id = win(w)->shortcutManager().bind(
            str(shortcut, len), [fn, user_data]() { fn(user_data); });
        if (id == 0) return KUI_ERR_INVALID_ARG;
        *out_id = id;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_unbind_shortcut(KuiWindow* w, uint32_t id) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->shortcutManager().unbind(static_cast<kui::ShortcutId>(id));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_unbind_all_shortcuts(KuiWindow* w) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->shortcutManager().unbindAll();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_window_set_shortcuts_enabled(KuiWindow* w, int enabled) {
    if (!w) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        win(w)->shortcutManager().setEnabled(enabled != 0);
        return KUI_OK;
    KUI_C_END
}

// --- Toasts --------------------------------------------------------------------

KuiStatus kui_document_show_toast(KuiDocument* d, const KuiCToastOptions* options,
                                  uint32_t* out_id) {
    if (!d || !options || !options->message || options->level < KUI_C_TOAST_INFO ||
        options->level > KUI_C_TOAST_ERROR)
        return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::ToastConfig config;
        config.message = str(options->message, options->message_len);
        if (options->title) config.title = str(options->title, options->title_len);
        config.level = static_cast<kui::ToastLevel>(options->level);
        config.durationMs = options->duration_ms;
        if (options->position) config.position = str(options->position, options->position_len);
        config.closable = options->closable != 0;
        if (options->icon) config.icon = str(options->icon, options->icon_len);
        const kui::u32 id = doc(d)->toastManager().show(config);
        if (out_id) *out_id = id;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_dismiss_toast(KuiDocument* d, uint32_t id) {
    if (!d) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        doc(d)->toastManager().dismiss(id);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_dismiss_all_toasts(KuiDocument* d) {
    if (!d) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        doc(d)->toastManager().dismissAll();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_get_toast_count(KuiDocument* d, int* out_count) {
    if (!d || !out_count) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        *out_count = doc(d)->toastManager().count();
        return KUI_OK;
    KUI_C_END
}

// --- Screenshot ----------------------------------------------------------------

KuiStatus kui_window_save_screenshot(KuiWindow* w, const char* path, size_t path_len,
                                     int* out_saved) {
    if (!w || !path) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::Renderer* renderer = win(w)->renderer();
        if (!renderer) return KUI_ERR_NOT_SUPPORTED;
        const bool saved = renderer->saveScreenshot(str(path, path_len));
        if (out_saved) *out_saved = saved ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

}  // extern "C"
