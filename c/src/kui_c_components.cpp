// =============================================================================
// kui_c_components.cpp — sections 18 and 19 of kui_c.h: built-in custom elements,
// ThemeManager, VirtualList, ListView, TreeView, and the document style profile (3.61.8).
// =============================================================================
#include "kui_c_internal.h"

#include <kui/component/builtin_components.h>
#include <kui/component/list_view.h>
#include <kui/component/theme.h>
#include <kui/component/tree_view.h>
#include <kui/component/virtual_list.h>
#include <kui/dom/document.h>
#include <kui/dom/element.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using kui_c::CallbackScope;
using kui_c::fromHandle;
using kui_c::str;
using kui_c::toHandle;
using kui_c::UserData;
using kui_c::UserDataPtr;
using kui_c::writeString;

struct KuiThemeManager {
    kui::ThemeManager value;
};

struct KuiVirtualList {
    kui::VirtualList value;
    UserDataPtr holder;
    int callbackDepth = 0;
    bool releasedInCallback = false;
};

struct KuiListView {
    kui::ListView value;
    UserDataPtr holder;
    int callbackDepth = 0;
    bool releasedInCallback = false;
};

struct KuiTreeView {
    kui::TreeView value;
    UserDataPtr holder;
    int callbackDepth = 0;
    bool releasedInCallback = false;
    std::vector<uint32_t> selection;  // backs on_selection_changed
    bool multi = false;               // config.multi_select of the last setup
};

namespace {

inline kui::Document* doc(KuiDocument* value) { return fromHandle<kui::Document>(value); }
inline kui::Element* elem(KuiElement* value) { return fromHandle<kui::Element>(value); }

bool validUnit(KuiCScrollUnit unit) { return unit == KUI_C_SCROLL_PIXEL || unit == KUI_C_SCROLL_LINE; }
kui::ScrollUnit nativeUnit(KuiCScrollUnit unit) {
    return unit == KUI_C_SCROLL_LINE ? kui::ScrollUnit::Line : kui::ScrollUnit::Pixel;
}

// Frees `object` now, or after its outermost callback returns.
template <typename T>
void releaseHandle(T* object) {
    if (object->callbackDepth > 0) {
        object->releasedInCallback = true;
        return;
    }
    delete object;
}

void fillState(const kui::VirtualList& list, KuiCVirtualListState* out) {
    out->visible_start = list.visibleStart();
    out->visible_end = list.visibleEnd();
    out->total_items = list.totalItems();
    out->recycle_pool_size = list.recyclePoolSize();
    out->focused_index = list.focusedIndex();
    out->total_height = list.totalHeight();
    out->item_height = list.itemHeight();
    out->scroll_unit = list.scrollUnit() == kui::ScrollUnit::Line ? KUI_C_SCROLL_LINE : KUI_C_SCROLL_PIXEL;
}

KuiStatus writeIndices(const std::vector<uint32_t>& values, uint32_t* buf, size_t capacity,
                       size_t* out_count) {
    *out_count = values.size();
    if (!buf) return KUI_OK;
    std::copy_n(values.begin(), (std::min)(capacity, values.size()), buf);
    return capacity < values.size() ? KUI_ERR_BUFFER_TOO_SMALL : KUI_OK;
}

void setColor(kui::String& target, const char* value) {
    if (value) target = value;
}

}  // namespace

// --- Built-in components -----------------------------------------------------------
KuiStatus kui_document_register_builtin_components(KuiDocument* value) {
    if (!value) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN kui::registerBuiltinComponents(doc(value)); return KUI_OK; KUI_C_END
}

// --- Themes ------------------------------------------------------------------------
KuiStatus kui_theme_manager_create(KuiThemeManager** out_manager) {
    if (!out_manager) return KUI_ERR_INVALID_ARG;
    *out_manager = nullptr;
    KUI_C_BEGIN *out_manager = new KuiThemeManager(); return KUI_OK; KUI_C_END
}

KuiStatus kui_theme_manager_release(KuiThemeManager* manager) {
    if (!manager) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN delete manager; return KUI_OK; KUI_C_END
}

KuiStatus kui_theme_manager_register(KuiThemeManager* manager, const char* name, size_t name_len,
                                     const KuiCKeyValue* variables, size_t variable_count) {
    if (!manager || !name || (variable_count && !variables)) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::Theme theme;
        theme.name = str(name, name_len);
        if (theme.name.empty()) return KUI_ERR_INVALID_ARG;
        for (size_t index = 0; index < variable_count; ++index) {
            const auto& entry = variables[index];
            if (!entry.key) return KUI_ERR_INVALID_ARG;
            const kui::String key = str(entry.key, entry.key_len);
            if (key.empty()) return KUI_ERR_INVALID_ARG;
            theme.variables[key] = entry.value ? str(entry.value, entry.value_len) : kui::String();
        }
        manager->value.registerTheme(theme.name, theme);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_theme_manager_apply(KuiThemeManager* manager, const char* name, size_t name_len,
                                  KuiDocument* document) {
    if (!manager || !name || !document) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const kui::String theme = str(name, name_len);
        if (!manager->value.getTheme(theme)) return KUI_ERR_NOT_FOUND;
        manager->value.apply(theme, doc(document));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_theme_manager_get_active(KuiThemeManager* manager, char* buf, size_t buf_capacity,
                                       size_t* out_len) {
    if (!manager) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN return writeString(manager->value.activeTheme(), buf, buf_capacity, out_len); KUI_C_END
}

KuiStatus kui_theme_manager_get_theme_count(KuiThemeManager* manager, size_t* out_count) {
    if (!manager || !out_count) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN *out_count = manager->value.themeNames().size(); return KUI_OK; KUI_C_END
}

KuiStatus kui_theme_manager_get_theme_name(KuiThemeManager* manager, size_t index, char* buf,
                                           size_t buf_capacity, size_t* out_len) {
    if (!manager) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const auto names = manager->value.themeNames();
        if (index >= names.size()) return KUI_ERR_NOT_FOUND;
        return writeString(names[index], buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_theme_manager_get_variable_count(KuiThemeManager* manager, const char* theme,
                                               size_t theme_len, size_t* out_count) {
    if (!manager || !theme || !out_count) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const auto* found = manager->value.getTheme(str(theme, theme_len));
        if (!found) return KUI_ERR_NOT_FOUND;
        *out_count = found->variables.size();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_theme_manager_get_variable(KuiThemeManager* manager, const char* theme,
                                         size_t theme_len, size_t index, char* name_buf,
                                         size_t name_capacity, size_t* out_name_len,
                                         char* value_buf, size_t value_capacity,
                                         size_t* out_value_len) {
    if (!manager || !theme) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const auto* found = manager->value.getTheme(str(theme, theme_len));
        if (!found || index >= found->variables.size()) return KUI_ERR_NOT_FOUND;
        auto it = found->variables.begin();
        std::advance(it, static_cast<std::ptrdiff_t>(index));
        const KuiStatus name = writeString(it->first, name_buf, name_capacity, out_name_len);
        const KuiStatus value = writeString(it->second, value_buf, value_capacity, out_value_len);
        if (name != KUI_OK && name != KUI_ERR_BUFFER_TOO_SMALL) return name;
        if (value != KUI_OK && value != KUI_ERR_BUFFER_TOO_SMALL) return value;
        return name != KUI_OK ? name : value;
    KUI_C_END
}

// --- VirtualList -------------------------------------------------------------------
KuiStatus kui_virtual_list_create(KuiElement* container, const KuiCVirtualListConfig* config,
                                  KuiVirtualList** out_list) {
    if (!out_list) return KUI_ERR_INVALID_ARG;
    *out_list = nullptr;
    if (!container || !config || !config->render_item || config->item_height < 0 ||
        !validUnit(config->scroll_unit))
        return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        if (!elem(container)->ownerDocument()) return KUI_ERR_INVALID_ARG;
        kui_c::ArmOnSuccess guard(std::make_shared<UserData>(config->user_data, config->release_fn));
        auto list = std::make_unique<KuiVirtualList>();
        list->holder = guard.holder;
        KuiVirtualList* self = list.get();
        const KuiCVirtualListConfig c = *config;
        kui::VirtualListConfig native;
        native.totalItems = c.total_items;
        native.itemHeight = c.item_height > 0 ? c.item_height : 32.0f;
        native.overscan = c.overscan;
        native.recyclePoolMax = c.recycle_pool_max;
        native.scrollUnit = nativeUnit(c.scroll_unit);
        native.enableKeyboard = c.enable_keyboard != 0;
        native.renderItem = [self, fn = c.render_item](kui::Document* owner, kui::u32 index) {
            CallbackScope<KuiVirtualList> scope(self);
            KuiElement* raw = fn(toHandle<KuiDocument>(owner), index, self->holder->data);
            return kui::RefPtr<kui::Element>(raw ? elem(raw) : nullptr, false);  // takes the reference
        };
        if (auto fn = c.height_for_item) {
            native.heightForItem = [self, fn](kui::u32 index) {
                CallbackScope<KuiVirtualList> scope(self);
                return fn(index, self->holder->data);
            };
        }
        if (auto fn = c.on_focus_change) {
            native.onFocusChange = [self, fn](kui::i32 oldIndex, kui::i32 newIndex) {
                CallbackScope<KuiVirtualList> scope(self);
                fn(oldIndex, newIndex, self->holder->data);
            };
        }
        if (auto fn = c.on_activate) {
            native.onActivate = [self, fn](kui::u32 index) {
                CallbackScope<KuiVirtualList> scope(self);
                fn(index, self->holder->data);
            };
        }
        list->value.setup(elem(container), native);
        guard.commit();
        *out_list = list.release();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_virtual_list_release(KuiVirtualList* list) {
    if (!list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN releaseHandle(list); return KUI_OK; KUI_C_END
}

KuiStatus kui_virtual_list_get_state(KuiVirtualList* list, KuiCVirtualListState* out_state) {
    if (!list || !out_state) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN fillState(list->value, out_state); return KUI_OK; KUI_C_END
}

KuiStatus kui_virtual_list_set_total_items(KuiVirtualList* list, uint32_t count) {
    if (!list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.setTotalItems(count); return KUI_OK; KUI_C_END
}

KuiStatus kui_virtual_list_insert_items(KuiVirtualList* list, uint32_t index, uint32_t count) {
    if (!list || index > list->value.totalItems()) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.insertItems(index, count); return KUI_OK; KUI_C_END
}

KuiStatus kui_virtual_list_remove_items(KuiVirtualList* list, uint32_t index, uint32_t count) {
    if (!list || index > list->value.totalItems() || count > list->value.totalItems() - index)
        return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.removeItems(index, count); return KUI_OK; KUI_C_END
}

KuiStatus kui_virtual_list_refresh(KuiVirtualList* list) {
    if (!list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.refresh(); return KUI_OK; KUI_C_END
}

KuiStatus kui_virtual_list_scroll_to_index(KuiVirtualList* list, uint32_t index) {
    if (!list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.scrollToIndex(index); return KUI_OK; KUI_C_END
}

KuiStatus kui_virtual_list_scroll_to_top(KuiVirtualList* list) {
    if (!list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.scrollToTop(); return KUI_OK; KUI_C_END
}

KuiStatus kui_virtual_list_scroll_to_bottom(KuiVirtualList* list) {
    if (!list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.scrollToBottom(); return KUI_OK; KUI_C_END
}

KuiStatus kui_virtual_list_set_scroll_unit(KuiVirtualList* list, KuiCScrollUnit unit) {
    if (!list || !validUnit(unit)) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.setScrollUnit(nativeUnit(unit)); return KUI_OK; KUI_C_END
}

KuiStatus kui_virtual_list_set_focused_index(KuiVirtualList* list, int32_t index) {
    if (!list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.setFocusedIndex(index); return KUI_OK; KUI_C_END
}

KuiStatus kui_virtual_list_move_focus(KuiVirtualList* list, int32_t delta) {
    if (!list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.moveFocus(delta); return KUI_OK; KUI_C_END
}

// --- ListView ----------------------------------------------------------------------
KuiStatus kui_list_view_create(KuiElement* container, const KuiCListViewConfig* config,
                               KuiListView** out_list) {
    if (!out_list) return KUI_ERR_INVALID_ARG;
    *out_list = nullptr;
    if (!container || !config || !config->item_provider || config->item_height < 0 ||
        !validUnit(config->scroll_unit) || config->selection_mode < KUI_C_SELECTION_NONE ||
        config->selection_mode > KUI_C_SELECTION_MULTI)
        return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        if (!elem(container)->ownerDocument()) return KUI_ERR_INVALID_ARG;
        kui_c::ArmOnSuccess guard(std::make_shared<UserData>(config->user_data, config->release_fn));
        auto list = std::make_unique<KuiListView>();
        list->holder = guard.holder;
        KuiListView* self = list.get();
        const KuiCListViewConfig c = *config;
        kui::ListViewConfig native;
        native.itemCount = c.item_count;
        if (c.item_height > 0) native.itemHeight = c.item_height;
        native.overscan = c.overscan;
        native.scrollUnit = nativeUnit(c.scroll_unit);
        native.selectionMode = static_cast<kui::ListViewConfig::SelectionMode>(c.selection_mode);
        setColor(native.itemBg, c.item_bg);
        setColor(native.itemHoverBg, c.item_hover_bg);
        setColor(native.itemSelectedBg, c.item_selected_bg);
        setColor(native.itemTextColor, c.item_text_color);
        setColor(native.itemSubColor, c.item_sub_color);
        native.itemProvider = [self, fn = c.item_provider](kui::u32 index) {
            CallbackScope<KuiListView> scope(self);
            KuiCListViewItem item{};
            fn(index, &item, self->holder->data);
            kui::ListViewItem result;
            if (item.text) result.text = str(item.text, item.text_len);
            if (item.subtitle) result.subtitle = str(item.subtitle, item.subtitle_len);
            if (item.icon) result.icon = str(item.icon, item.icon_len);
            result.disabled = item.disabled != 0;
            return result;
        };
        if (auto fn = c.on_select) {
            native.onSelect = [self, fn](kui::u32 index) {
                CallbackScope<KuiListView> scope(self);
                fn(index, self->holder->data);
            };
        }
        if (auto fn = c.on_activate) {
            native.onActivate = [self, fn](kui::u32 index) {
                CallbackScope<KuiListView> scope(self);
                fn(index, self->holder->data);
            };
        }
        list->value.setup(elem(container), native);
        guard.commit();
        *out_list = list.release();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_list_view_release(KuiListView* list) {
    if (!list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN releaseHandle(list); return KUI_OK; KUI_C_END
}

KuiStatus kui_list_view_select(KuiListView* list, uint32_t index) {
    if (!list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.select(index); return KUI_OK; KUI_C_END
}

KuiStatus kui_list_view_deselect(KuiListView* list, uint32_t index) {
    if (!list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.deselect(index); return KUI_OK; KUI_C_END
}

KuiStatus kui_list_view_clear_selection(KuiListView* list) {
    if (!list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.clearSelection(); return KUI_OK; KUI_C_END
}

KuiStatus kui_list_view_select_range(KuiListView* list, uint32_t start, uint32_t end) {
    if (!list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.selectRange(start, end); return KUI_OK; KUI_C_END
}

KuiStatus kui_list_view_is_selected(KuiListView* list, uint32_t index, int* out_selected) {
    if (!list || !out_selected) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN *out_selected = list->value.isSelected(index) ? 1 : 0; return KUI_OK; KUI_C_END
}

KuiStatus kui_list_view_get_selected(KuiListView* list, uint32_t* buf, size_t buf_capacity,
                                     size_t* out_count) {
    if (!list || !out_count) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const auto selected = list->value.selectedIndices();
        return writeIndices(std::vector<uint32_t>(selected.begin(), selected.end()), buf,
                            buf_capacity, out_count);
    KUI_C_END
}

KuiStatus kui_list_view_get_focused_index(KuiListView* list, int32_t* out_index) {
    if (!list || !out_index) return KUI_ERR_INVALID_ARG;
    *out_index = list->value.focusedIndex();
    return KUI_OK;
}

KuiStatus kui_list_view_set_item_count(KuiListView* list, uint32_t count) {
    if (!list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.setItemCount(count); return KUI_OK; KUI_C_END
}

KuiStatus kui_list_view_refresh(KuiListView* list) {
    if (!list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.refresh(); return KUI_OK; KUI_C_END
}

KuiStatus kui_list_view_scroll_to_index(KuiListView* list, uint32_t index) {
    if (!list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.scrollToIndex(index); return KUI_OK; KUI_C_END
}

KuiStatus kui_list_view_scroll_to_top(KuiListView* list) {
    if (!list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.scrollToTop(); return KUI_OK; KUI_C_END
}

KuiStatus kui_list_view_scroll_to_bottom(KuiListView* list) {
    if (!list) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.scrollToBottom(); return KUI_OK; KUI_C_END
}

KuiStatus kui_list_view_set_scroll_unit(KuiListView* list, KuiCScrollUnit unit) {
    if (!list || !validUnit(unit)) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN list->value.setScrollUnit(nativeUnit(unit)); return KUI_OK; KUI_C_END
}

KuiStatus kui_list_view_get_state(KuiListView* list, KuiCVirtualListState* out_state) {
    if (!list || !out_state) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN fillState(list->value.virtualList(), out_state); return KUI_OK; KUI_C_END
}

// --- TreeView ----------------------------------------------------------------------
KuiStatus kui_tree_view_create(KuiTreeView** out_tree) {
    if (!out_tree) return KUI_ERR_INVALID_ARG;
    *out_tree = nullptr;
    KUI_C_BEGIN *out_tree = new KuiTreeView(); return KUI_OK; KUI_C_END
}

KuiStatus kui_tree_view_release(KuiTreeView* tree) {
    if (!tree) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN releaseHandle(tree); return KUI_OK; KUI_C_END
}

KuiStatus kui_tree_view_add_node(KuiTreeView* tree, const char* text, size_t text_len,
                                 const char* icon, size_t icon_len, uint32_t* out_index) {
    if (!tree || !text) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const auto index = tree->value.addNode(str(text, text_len), icon ? str(icon, icon_len) : kui::String());
        if (out_index) *out_index = index;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_tree_view_add_child(KuiTreeView* tree, uint32_t parent, const char* text,
                                  size_t text_len, const char* icon, size_t icon_len,
                                  uint32_t* out_index) {
    if (!tree || !text || parent >= tree->value.nodeCount()) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        const auto index = tree->value.addChild(parent, str(text, text_len),
                                                icon ? str(icon, icon_len) : kui::String());
        if (out_index) *out_index = index;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_tree_view_get_node_count(KuiTreeView* tree, uint32_t* out_count) {
    if (!tree || !out_count) return KUI_ERR_INVALID_ARG;
    *out_count = tree->value.nodeCount();
    return KUI_OK;
}

KuiStatus kui_tree_view_get_node(KuiTreeView* tree, uint32_t index, KuiCTreeNode* out_node) {
    if (!tree || !out_node) return KUI_ERR_INVALID_ARG;
    if (index >= tree->value.nodeCount()) return KUI_ERR_NOT_FOUND;
    const auto& node = tree->value.node(index);
    out_node->text = node.text.c_str();
    out_node->text_len = node.text.size();
    out_node->icon = node.icon.c_str();
    out_node->icon_len = node.icon.size();
    out_node->expanded = node.expanded ? 1 : 0;
    out_node->disabled = node.disabled ? 1 : 0;
    out_node->parent = node.parentIndex;
    out_node->child_count = node.children.size();
    return KUI_OK;
}

KuiStatus kui_tree_view_get_node_child(KuiTreeView* tree, uint32_t index, size_t position,
                                       uint32_t* out_child) {
    if (!tree || !out_child) return KUI_ERR_INVALID_ARG;
    if (index >= tree->value.nodeCount()) return KUI_ERR_NOT_FOUND;
    const auto& children = tree->value.node(index).children;
    if (position >= children.size()) return KUI_ERR_NOT_FOUND;
    *out_child = children[position];
    return KUI_OK;
}

KuiStatus kui_tree_view_set_node(KuiTreeView* tree, uint32_t index, const char* text, size_t text_len,
                                 const char* icon, size_t icon_len, int disabled) {
    if (!tree) return KUI_ERR_INVALID_ARG;
    if (index >= tree->value.nodeCount()) return KUI_ERR_NOT_FOUND;
    KUI_C_BEGIN
        auto& node = tree->value.node(index);
        if (text) node.text = str(text, text_len);
        if (icon) node.icon = str(icon, icon_len);
        node.disabled = disabled != 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_tree_view_setup(KuiTreeView* tree, KuiElement* container,
                              const KuiCTreeViewConfig* config) {
    if (!tree || !container || !config || config->item_height < 0 || config->indent_width < 0 ||
        !validUnit(config->scroll_unit))
        return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        if (!elem(container)->ownerDocument()) return KUI_ERR_INVALID_ARG;
        kui_c::ArmOnSuccess guard(std::make_shared<UserData>(config->user_data, config->release_fn));
        KuiTreeView* self = tree;
        const KuiCTreeViewConfig c = *config;
        const UserDataPtr holder = guard.holder;
        kui::TreeViewConfig native;
        if (c.item_height > 0) native.itemHeight = c.item_height;
        if (c.indent_width > 0) native.indentWidth = c.indent_width;
        native.overscan = c.overscan;
        native.scrollUnit = nativeUnit(c.scroll_unit);
        setColor(native.itemBg, c.item_bg);
        setColor(native.itemHoverBg, c.item_hover_bg);
        setColor(native.itemSelectedBg, c.item_selected_bg);
        setColor(native.itemTextColor, c.item_text_color);
        native.multiSelect = c.multi_select != 0;
        if (auto fn = c.on_select) {
            native.onSelect = [self, holder, fn](kui::u32 index) {
                CallbackScope<KuiTreeView> scope(self);
                fn(index, holder->data);
            };
        }
        if (auto fn = c.on_toggle) {
            native.onToggle = [self, holder, fn](kui::u32 index, bool expanded) {
                CallbackScope<KuiTreeView> scope(self);
                fn(index, expanded ? 1 : 0, holder->data);
            };
        }
        if (auto fn = c.on_selection_changed) {
            native.onSelectionChanged = [self, holder, fn](const std::set<kui::u32>& selected) {
                CallbackScope<KuiTreeView> scope(self);
                self->selection.assign(selected.begin(), selected.end());
                fn(self->selection.empty() ? nullptr : self->selection.data(), self->selection.size(),
                   holder->data);
            };
        }
        tree->value.setup(elem(container), native);
        tree->multi = native.multiSelect;
        tree->holder = holder;  // a second setup releases the previous user_data
        guard.commit();
        return KUI_OK;
    KUI_C_END
}

#define KUI_C_TREE_CALL(expr)                    \
    if (!tree) return KUI_ERR_INVALID_ARG;       \
    KUI_C_BEGIN expr; return KUI_OK; KUI_C_END

#define KUI_C_TREE_NODE_CALL(expr)                                              \
    if (!tree) return KUI_ERR_INVALID_ARG;                                      \
    if (index >= tree->value.nodeCount()) return KUI_ERR_NOT_FOUND;             \
    KUI_C_BEGIN expr; return KUI_OK; KUI_C_END

KuiStatus kui_tree_view_expand(KuiTreeView* tree, uint32_t index) { KUI_C_TREE_NODE_CALL(tree->value.expand(index)) }
KuiStatus kui_tree_view_collapse(KuiTreeView* tree, uint32_t index) { KUI_C_TREE_NODE_CALL(tree->value.collapse(index)) }
KuiStatus kui_tree_view_toggle(KuiTreeView* tree, uint32_t index) { KUI_C_TREE_NODE_CALL(tree->value.toggle(index)) }
KuiStatus kui_tree_view_expand_all(KuiTreeView* tree) { KUI_C_TREE_CALL(tree->value.expandAll()) }
KuiStatus kui_tree_view_collapse_all(KuiTreeView* tree) { KUI_C_TREE_CALL(tree->value.collapseAll()) }
KuiStatus kui_tree_view_select(KuiTreeView* tree, uint32_t index) { KUI_C_TREE_NODE_CALL(tree->value.select(index)) }
KuiStatus kui_tree_view_clear_selection(KuiTreeView* tree) { KUI_C_TREE_CALL(tree->value.clearSelection()) }
KuiStatus kui_tree_view_toggle_select(KuiTreeView* tree, uint32_t index) { KUI_C_TREE_NODE_CALL(tree->value.toggleSelect(index)) }
KuiStatus kui_tree_view_select_range(KuiTreeView* tree, uint32_t index) { KUI_C_TREE_NODE_CALL(tree->value.selectRange(index)) }
KuiStatus kui_tree_view_select_all(KuiTreeView* tree) { KUI_C_TREE_CALL(tree->value.selectAll()) }
KuiStatus kui_tree_view_refresh(KuiTreeView* tree) { KUI_C_TREE_CALL(tree->value.refresh()) }
KuiStatus kui_tree_view_scroll_to_node(KuiTreeView* tree, uint32_t index) { KUI_C_TREE_NODE_CALL(tree->value.scrollToNode(index)) }

#undef KUI_C_TREE_CALL
#undef KUI_C_TREE_NODE_CALL

KuiStatus kui_tree_view_set_scroll_unit(KuiTreeView* tree, KuiCScrollUnit unit) {
    if (!tree || !validUnit(unit)) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN tree->value.setScrollUnit(nativeUnit(unit)); return KUI_OK; KUI_C_END
}

KuiStatus kui_tree_view_is_selected(KuiTreeView* tree, uint32_t index, int* out_selected) {
    if (!tree || !out_selected) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN *out_selected = tree->value.isSelected(index) ? 1 : 0; return KUI_OK; KUI_C_END
}

KuiStatus kui_tree_view_get_selected_index(KuiTreeView* tree, int32_t* out_index) {
    if (!tree || !out_index) return KUI_ERR_INVALID_ARG;
    *out_index = tree->value.selectedIndex();
    return KUI_OK;
}

KuiStatus kui_tree_view_get_selected(KuiTreeView* tree, uint32_t* buf, size_t buf_capacity,
                                     size_t* out_count) {
    if (!tree || !out_count) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        // Same answer as is_selected: the set in a multi-select tree, the one node otherwise.
        std::vector<uint32_t> values;
        if (tree->multi) {
            const auto& selected = tree->value.selectedIndices();
            values.assign(selected.begin(), selected.end());
        } else if (tree->value.selectedIndex() >= 0) {
            values.push_back(static_cast<uint32_t>(tree->value.selectedIndex()));
        }
        return writeIndices(values, buf, buf_capacity, out_count);
    KUI_C_END
}

KuiStatus kui_tree_view_get_visible_row_count(KuiTreeView* tree, uint32_t* out_count) {
    if (!tree || !out_count) return KUI_ERR_INVALID_ARG;
    *out_count = tree->value.visibleRowCount();
    return KUI_OK;
}

// --- Section 19: document style profile ---------------------------------------------
KuiStatus kui_document_set_style_profile(KuiDocument* value, KuiCStyleProfile profile) {
    if (!value || (profile != KUI_C_STYLE_NATIVE_GUI && profile != KUI_C_STYLE_BROWSER_COMPAT))
        return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        doc(value)->setStyleProfile(profile == KUI_C_STYLE_BROWSER_COMPAT
                                        ? kui::Document::StyleProfile::BrowserCompat
                                        : kui::Document::StyleProfile::NativeGui);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_get_style_profile(KuiDocument* value, KuiCStyleProfile* out_profile) {
    if (!value || !out_profile) return KUI_ERR_INVALID_ARG;
    *out_profile = doc(value)->styleProfile() == kui::Document::StyleProfile::BrowserCompat
                       ? KUI_C_STYLE_BROWSER_COMPAT
                       : KUI_C_STYLE_NATIVE_GUI;
    return KUI_OK;
}
