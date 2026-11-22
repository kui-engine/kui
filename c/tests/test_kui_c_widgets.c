/* Section 12 (3.61.8) regression tests: popups, the <input> picker and game
 * audio. C11. Creates one real window. Audio only checks the paths that do not
 * need a device: tests run with KUI_MEDIA_NO_DEVICE=1, so play() reports
 * KUI_ERR_NOT_SUPPORTED unless a real device and plugin are present. */
#include <kui_c.h>

#include <stdio.h>
#include <string.h>

static int passed = 0;
static int failed = 0;

#define CHECK(condition, message)                                             \
    do {                                                                       \
        if (condition) ++passed;                                               \
        else { ++failed; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, message); } \
    } while (0)

#define CHECK_OK(expression, message) CHECK((expression) == KUI_OK, message)

static int releases = 0;
static void count_release(void* user_data) {
    (void)user_data;
    ++releases;
}
static void on_menu(uint32_t id, void* user_data) {
    (void)id;
    (void)user_data;
}

static void test_popups(KuiDocument* doc) {
    KuiRect anchor = {100, 100, 80, 20};
    uint32_t tip = 0;
    CHECK_OK(kui_document_show_tooltip(doc, "hello", 0, &anchor, KUI_C_POPUP_BOTTOM, &tip), "show tooltip");
    CHECK(tip != 0, "tooltip id");
    int has = 0;
    kui_document_has_popup_of_type(doc, KUI_C_POPUP_TOOLTIP, &has);
    CHECK(has == 1, "tooltip is shown");
    CHECK(kui_document_show_tooltip(doc, "x", 0, &anchor, (KuiCPopupPosition)9, &tip) == KUI_ERR_INVALID_ARG,
          "position validated");

    KuiCMenuItem items[3];
    memset(items, 0, sizeof items);
    items[0].text = "Copy"; items[0].id = 1;
    items[1].separator = 1;
    items[2].text = "Paste"; items[2].id = 2; items[2].disabled = 1;
    releases = 0;
    uint32_t menu = 0;
    CHECK_OK(kui_document_show_context_menu(doc, 50, 60, items, 3, on_menu, NULL, count_release, &menu),
             "show context menu");
    kui_document_has_popup_of_type(doc, KUI_C_POPUP_CONTEXT_MENU, &has);
    CHECK(has == 1 && menu != 0, "context menu is shown");
    CHECK(releases == 0, "menu still referenced");
    CHECK_OK(kui_document_dismiss_popups_of_type(doc, KUI_C_POPUP_CONTEXT_MENU), "dismiss menus");
    kui_document_has_popup_of_type(doc, KUI_C_POPUP_CONTEXT_MENU, &has);
    CHECK(has == 0 && releases == 1, "dismissed menu releases user_data");
    items[0].text = NULL;
    CHECK(kui_document_show_context_menu(doc, 0, 0, items, 3, on_menu, NULL, count_release, &menu) ==
              KUI_ERR_INVALID_ARG, "item without text rejected");
    CHECK(releases == 1, "rejected menu does not release user_data");

    CHECK_OK(kui_document_dismiss_all_popups(doc), "dismiss all");
    kui_document_has_popups(doc, &has);
    CHECK(has == 0, "no popups left");

    CHECK_OK(kui_document_set_tooltip_delay(doc, 250), "tooltip delay");
    double delay = 0;
    kui_document_get_tooltip_delay(doc, &delay);
    CHECK(delay == 250, "tooltip delay read back");
    KuiCTooltipTheme theme = {0x11223344u, 0xAABBCCDDu, 0x01020304u, 6.0f};
    CHECK_OK(kui_document_set_tooltip_theme(doc, &theme), "tooltip theme");
    KuiCTooltipTheme back;
    memset(&back, 0, sizeof back);
    kui_document_get_tooltip_theme(doc, &back);
    CHECK(back.background == 0x11223344u && back.foreground == 0xAABBCCDDu &&
              back.border == 0x01020304u && back.radius == 6.0f, "tooltip theme read back");
    CHECK_OK(kui_document_set_popup_mode(doc, KUI_C_POPUP_MODE_OVERLAY), "popup mode");
    CHECK(kui_document_set_popup_mode(doc, (KuiCPopupMode)5) == KUI_ERR_INVALID_ARG, "popup mode validated");

    int menu_on = -1;
    CHECK_OK(kui_document_get_default_context_menu_enabled(doc, &menu_on), "default context menu state");
    CHECK(menu_on == 0, "the built-in context menu is off by default");
    CHECK_OK(kui_document_set_default_context_menu_enabled(doc, 1), "enable default context menu");
    kui_document_get_default_context_menu_enabled(doc, &menu_on);
    CHECK(menu_on == 1, "default context menu enabled");
    CHECK_OK(kui_document_set_default_context_menu_enabled(doc, 0), "disable default context menu");
    CHECK(kui_document_get_default_context_menu_enabled(doc, NULL) == KUI_ERR_INVALID_ARG,
          "default context menu: out pointer required");
}

static int changes = 0;
static void on_change(KuiEvent* event, void* user_data) {
    (void)event;
    (void)user_data;
    ++changes;
}

static void test_picker(KuiDocument* doc) {
    kui_document_load_html(doc, "<html><body><input id='d' type='date' value='2026-11-16'>"
                                "<input id='c' type='color' value='#ff0000'>"
                                "<input id='t' type='text'><p id='p'>p</p></body></html>", 0);
    for (int i = 0; i < 5; ++i) kui_platform_wait_events(0.01);
    KuiElement* date = NULL;
    KuiElement* color = NULL;
    KuiElement* text = NULL;
    KuiElement* para = NULL;
    kui_document_get_element_by_id(doc, "d", 0, &date);
    kui_document_get_element_by_id(doc, "c", 0, &color);
    kui_document_get_element_by_id(doc, "t", 0, &text);
    kui_document_get_element_by_id(doc, "p", 0, &para);

    CHECK_OK(kui_element_open_input_picker(date, KUI_C_PICKER_NONE), "open date picker by type");
    KuiCInputPickerState state;
    memset(&state, 0, sizeof state);
    CHECK_OK(kui_document_get_input_picker_state(doc, &state), "picker state");
    CHECK(state.open == 1 && state.enabled == 1 && state.mode == KUI_C_PICKER_DATE, "date picker open");
    CHECK(state.visible_year == 2026 && state.visible_month == 11, "calendar shows the input's month");
    CHECK_OK(kui_document_set_input_picker_month(doc, 2027, 2), "change month");
    kui_document_get_input_picker_state(doc, &state);
    CHECK(state.visible_year == 2027 && state.visible_month == 2, "month changed");
    CHECK(kui_document_set_input_picker_month(doc, 2027, 13) == KUI_ERR_INVALID_ARG, "month validated");
    KuiElement* target = NULL;
    CHECK_OK(kui_document_get_input_picker_target(doc, &target), "picker target");
    CHECK(target == date, "target is the date input");
    if (target) kui_element_release(target);
    CHECK_OK(kui_document_close_input_picker(doc), "close");
    kui_document_get_input_picker_state(doc, &state);
    CHECK(state.open == 0, "closed");
    CHECK(kui_document_get_input_picker_target(doc, &target) == KUI_ERR_NOT_FOUND, "no target when closed");

    KuiListenerId listener = 0;
    kui_element_add_event_listener(color, "change", 0, on_change, NULL, &listener);
    CHECK_OK(kui_element_open_input_picker(color, KUI_C_PICKER_COLOR), "open colour picker");
    CHECK_OK(kui_document_set_input_picker_color_hsv(doc, 120.0f, 1.0f, 1.0f), "set hsv");
    kui_document_get_input_picker_state(doc, &state);
    CHECK(state.mode == KUI_C_PICKER_COLOR && state.color_h == 120.0f, "hsv read back");
    CHECK(kui_document_set_input_picker_color_hsv(doc, 400.0f, 1.0f, 1.0f) == KUI_ERR_INVALID_ARG,
          "hue validated");
    CHECK_OK(kui_document_commit_input_picker(doc), "commit");
    char value[32];
    size_t len = 0;
    kui_element_get_attribute(color, "value", 0, value, sizeof value, &len);
    CHECK(changes == 1, "commit fires change");

    CHECK(kui_element_open_input_picker(text, KUI_C_PICKER_NONE) == KUI_ERR_NOT_SUPPORTED,
          "text inputs have no panel");
    CHECK(kui_element_open_input_picker(para, KUI_C_PICKER_DATE) == KUI_ERR_INVALID_ARG,
          "only <input> elements");
    CHECK_OK(kui_document_set_input_picker_enabled(doc, 0), "disable picker");
    CHECK(kui_element_open_input_picker(date, KUI_C_PICKER_NONE) == KUI_ERR_NOT_SUPPORTED,
          "disabled picker does not open");
    CHECK_OK(kui_document_set_input_picker_enabled(doc, 1), "enable picker");
    CHECK_OK(kui_element_open_input_picker(date, KUI_C_PICKER_TIME), "open time picker explicitly");
    CHECK_OK(kui_document_set_input_picker_time(doc, 9, 30), "set time");
    kui_document_get_input_picker_state(doc, &state);
    CHECK(state.mode == KUI_C_PICKER_TIME && state.hour == 9 && state.minute == 30, "time read back");
    CHECK(kui_document_set_input_picker_time(doc, 24, 0) == KUI_ERR_INVALID_ARG, "hour validated");
    kui_document_close_input_picker(doc);

    kui_element_release(date);
    kui_element_release(color);
    kui_element_release(text);
    kui_element_release(para);
}

static void test_audio(KuiDocument* doc) {
    int available = -1;
    CHECK_OK(kui_document_audio_is_available(doc, &available), "audio availability");
    uint32_t handle = 99;
    const KuiStatus played = kui_document_audio_play(doc, "missing.wav", 0, NULL, &handle);
    if (available) {
        CHECK(played == KUI_ERR_IO && handle == 0, "a missing file cannot play");
    } else {
        CHECK(played == KUI_ERR_NOT_SUPPORTED && handle == 0, "no device: play is not supported");
    }
    KuiCAudioPlayOptions options;
    memset(&options, 0, sizeof options);
    options.group = (KuiCAudioGroup)7;
    CHECK(kui_document_audio_play(doc, "a.wav", 0, &options, &handle) == KUI_ERR_INVALID_ARG,
          "group validated");
    KuiCSoundInfo info;
    CHECK(kui_document_audio_get_info(doc, 12345, &info) == KUI_ERR_NOT_FOUND, "unknown handle");
    CHECK_OK(kui_document_audio_stop(doc, 12345), "stopping an unknown handle is harmless");

    CHECK_OK(kui_document_audio_set_group(doc, KUI_C_AUDIO_MUSIC, 0.25f, 1), "set group");
    float volume = 0;
    int muted = 0;
    CHECK_OK(kui_document_audio_get_group(doc, KUI_C_AUDIO_MUSIC, &volume, &muted), "get group");
    CHECK(volume == 0.25f && muted == 1, "group read back");
    CHECK(kui_document_audio_set_group(doc, KUI_C_AUDIO_MUSIC, 2.0f, 0) == KUI_ERR_INVALID_ARG,
          "volume validated");
    CHECK_OK(kui_document_audio_set_group(doc, KUI_C_AUDIO_MUSIC, 1.0f, 0), "restore group");
    CHECK_OK(kui_document_audio_stop_all(doc, -1), "stop all");
    CHECK_OK(kui_document_audio_set_all_paused(doc, KUI_C_AUDIO_SFX, 1), "pause group");
    CHECK(kui_document_audio_stop_all(doc, 9) == KUI_ERR_INVALID_ARG, "stop_all group validated");
    uint32_t active = 9, pooled = 9;
    CHECK_OK(kui_document_audio_get_counts(doc, -1, &active, &pooled), "counts");
    CHECK(active == 0, "nothing playing");
    CHECK_OK(kui_document_audio_unload(doc, NULL, 0), "unload all");
}

/* -- Section 15: drag-and-drop data -------------------------------------- */
static int drag_started = 0, dropped = 0, data_ok = 0, types_ok = 0, effect_ok = 0;
static void on_dragstart(KuiEvent* event, void* user_data) {
    (void)user_data;
    ++drag_started;
    kui_event_drag_set_data(event, "text/plain", 0, "hello drag", 0);
    kui_event_drag_set_data(event, "application/x-kui", 0, "42", 0);
    kui_event_drag_set_effect_allowed(event, KUI_C_EFFECT_COPY);
}
static void on_dragover(KuiEvent* event, void* user_data) {
    (void)user_data;
    kui_event_prevent_default(event);
    kui_event_drag_set_drop_effect(event, KUI_C_DROP_COPY);
}
static void on_drop(KuiEvent* event, void* user_data) {
    (void)user_data;
    ++dropped;
    char buf[64];
    size_t len = 0;
    if (kui_event_drag_get_data(event, "text/plain", 0, buf, sizeof buf, &len) == KUI_OK &&
        strcmp(buf, "hello drag") == 0) data_ok = 1;
    size_t count = 0;
    kui_event_drag_get_type_count(event, &count);
    char type0[64] = {0};
    if (count >= 2 && kui_event_drag_get_type(event, 0, type0, sizeof type0, &len) == KUI_OK &&
        strcmp(type0, "text/plain") == 0 &&
        kui_event_drag_get_data(event, "missing/type", 0, buf, sizeof buf, &len) == KUI_ERR_NOT_FOUND)
        types_ok = 1;
    KuiCEffectAllowed allowed = KUI_C_EFFECT_NONE;
    kui_event_drag_get_effect_allowed(event, &allowed);
    if (allowed == KUI_C_EFFECT_COPY) effect_ok = 1;
    kui_event_drag_clear_data(event, NULL, 0);
    kui_event_drag_get_type_count(event, &count);
    if (count != 0) effect_ok = 0;
}
static void on_click(KuiEvent* event, void* user_data) {
    (void)user_data;
    size_t count = 0;
    /* a plain mouse event is not a drag event */
    if (kui_event_drag_get_type_count(event, &count) == KUI_ERR_NOT_SUPPORTED) ++*(int*)user_data;
}

static void test_drag_drop(KuiDocument* doc) {
    kui_document_load_html(doc,
        "<html><body style='margin:0'>"
        "<div id='src' draggable='true' style='position:absolute;left:10px;top:10px;width:80px;height:80px'>s</div>"
        "<div id='dst' style='position:absolute;left:200px;top:10px;width:80px;height:80px'>d</div>"
        "</body></html>", 0);
    for (int i = 0; i < 5; ++i) kui_platform_wait_events(0.01);
    KuiElement* src = NULL;
    KuiElement* dst = NULL;
    kui_document_get_element_by_id(doc, "src", 0, &src);
    kui_document_get_element_by_id(doc, "dst", 0, &dst);
    KuiListenerId id = 0;
    int plain = 0;
    kui_element_add_event_listener(src, "dragstart", 0, on_dragstart, NULL, &id);
    kui_element_add_event_listener(dst, "dragover", 0, on_dragover, NULL, &id);
    kui_element_add_event_listener(dst, "drop", 0, on_drop, NULL, &id);
    kui_element_add_event_listener(dst, "mousedown", 0, on_click, &plain, &id);

    kui_devtools_dispatch_mouse_event(doc, KUI_C_MOUSE_PRESSED, 50, 50, KUI_C_MOUSE_BUTTON_LEFT, 0, 1, 0, 0);
    for (int x = 60; x <= 240; x += 30) {
        kui_devtools_dispatch_mouse_event(doc, KUI_C_MOUSE_MOVED, (float)x, 50, KUI_C_MOUSE_BUTTON_LEFT, 0, 0, 0, 0);
    }
    kui_devtools_dispatch_mouse_event(doc, KUI_C_MOUSE_RELEASED, 240, 50, KUI_C_MOUSE_BUTTON_LEFT, 0, 1, 0, 0);
    CHECK(drag_started == 1, "dragstart on the draggable source");
    CHECK(dropped == 1, "drop on the target that allowed it");
    CHECK(data_ok, "drop reads the data set in dragstart");
    CHECK(types_ok, "types list and missing format");
    CHECK(effect_ok, "effectAllowed read back and clear_data empties the types");

    kui_devtools_dispatch_mouse_event(doc, KUI_C_MOUSE_PRESSED, 240, 50, KUI_C_MOUSE_BUTTON_LEFT, 0, 1, 0, 0);
    kui_devtools_dispatch_mouse_event(doc, KUI_C_MOUSE_RELEASED, 240, 50, KUI_C_MOUSE_BUTTON_LEFT, 0, 1, 0, 0);
    CHECK(plain >= 1, "a plain mouse event is not a drag event (KUI_ERR_NOT_SUPPORTED)");
    kui_element_release(src);
    kui_element_release(dst);
}

/* -- <kui-context-menu>: right-click opens the declarative menu; menu-open's relatedTarget ------ */
static char ctx_related[32];
static int ctx_opens = 0;
static void on_menu_open(KuiEvent* event, void* user_data) {
    (void)user_data;
    KuiElement* related = NULL;
    size_t len = 0;
    ++ctx_opens;
    if (kui_event_get_related_target(event, &related) == KUI_OK) {
        kui_element_get_id(related, ctx_related, sizeof ctx_related, &len);
        kui_element_release(related);
    }
}

static void test_declarative_context_menu(KuiDocument* doc) {
    kui_document_load_html(doc,
        "<html><body style='margin:0'>"
        "<div id='row' style='width:200px;height:60px'>row</div>"
        "<kui-context-menu id='menu' for='row'><kui-menu-item text='Open'></kui-menu-item></kui-context-menu>"
        "</body></html>", 0);
    for (int i = 0; i < 5; ++i) kui_platform_wait_events(0.01);
    KuiElement* menu = NULL;
    KuiElement* row = NULL;
    kui_document_get_element_by_id(doc, "menu", 0, &menu);
    kui_document_get_element_by_id(doc, "row", 0, &row);
    KuiListenerId id = 0;
    CHECK_OK(kui_element_add_event_listener(menu, "menu-open", 0, on_menu_open, NULL, &id), "listen menu-open");
    kui_devtools_dispatch_mouse_event(doc, KUI_C_MOUSE_PRESSED, 20, 20, KUI_C_MOUSE_BUTTON_RIGHT, 0, 1, 0, 0);
    kui_devtools_dispatch_mouse_event(doc, KUI_C_MOUSE_RELEASED, 20, 20, KUI_C_MOUSE_BUTTON_RIGHT, 0, 1, 0, 0);
    int open = 0;
    kui_element_has_attribute(menu, "open", 0, &open);
    CHECK(ctx_opens == 1 && open, "right-click opens the <kui-context-menu>");
    CHECK(strcmp(ctx_related, "row") == 0, "menu-open relatedTarget is the right-clicked element");
    /* a press outside closes it */
    kui_devtools_dispatch_mouse_event(doc, KUI_C_MOUSE_PRESSED, 300, 300, KUI_C_MOUSE_BUTTON_LEFT, 0, 1, 0, 0);
    kui_devtools_dispatch_mouse_event(doc, KUI_C_MOUSE_RELEASED, 300, 300, KUI_C_MOUSE_BUTTON_LEFT, 0, 1, 0, 0);
    kui_element_has_attribute(menu, "open", 0, &open);
    CHECK(!open, "a press outside closes it");
    /* events without a relatedTarget report KUI_ERR_NOT_FOUND */
    CHECK(kui_event_get_related_target(NULL, NULL) == KUI_ERR_INVALID_ARG, "related target: null event");
    kui_element_release(menu);
    kui_element_release(row);
}

/* -- Section 17: custom element paint, tick and input --------------------- */
typedef struct CeState {
    int connected, paints, ticks, keys, mouse_downs, wheels, ime_updates, ime_commits, released;
    int need_ticks, draw_ok, restore_unbalanced;
    float paint_w, paint_h, dpr, mouse_x, mouse_y, wheel_dy;
    int mouse_button;
    char key[16], update[32], commit[32];
} CeState;
static CeState ce;
static int ce_released_other = 0;

static void ce_connected(KuiElement* e, void* ud) { (void)e; ++((CeState*)ud)->connected; }
static void ce_paint(KuiElement* e, KuiPaintContext* p, void* ud) {
    CeState* s = (CeState*)ud;
    static const uint32_t pixels[4] = {0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u};
    KuiCTextStyle style;
    KuiCCornerRadii radii = {4, 4, 4, 4, 6, 3, 0, 0};
    KuiCCornerRadii bad = {-1, 0, 0, 0, 0, 0, 0, 0};
    KuiRect src = {0, 0, 16, 16}, dest = {60, 2, 16, 16};
    (void)e;
    memset(&style, 0, sizeof style);
    style.font_size = 12;
    style.font_weight = 700;
    ++s->paints;
    kui_paint_get_info(p, &s->paint_w, &s->paint_h, &s->dpr);
    s->draw_ok = kui_paint_fill_rect(p, 0, 0, s->paint_w, s->paint_h, 0xFF0000FFu) == KUI_OK &&
                 kui_paint_save(p) == KUI_OK &&
                 kui_paint_clip_rect(p, 0, 0, 50, 50) == KUI_OK &&
                 kui_paint_fill_rounded_rect(p, 2, 2, 20, 20, &radii, 0x0000FFFFu) == KUI_OK &&
                 kui_paint_fill_rounded_rect(p, 2, 2, 20, 20, &bad, 0x0000FFFFu) == KUI_ERR_INVALID_ARG &&
                 kui_paint_stroke_rect(p, 2, 2, 20, 20, 1, 0x000000FFu) == KUI_OK &&
                 kui_paint_stroke_rounded_rect(p, 2, 2, 20, 20, &radii, 1, 0x000000FFu) == KUI_OK &&
                 kui_paint_clip_rounded_rect(p, 0, 0, 50, 50, &radii) == KUI_OK &&
                 kui_paint_translate(p, 1, 1) == KUI_OK &&
                 kui_paint_scale(p, 1, 1) == KUI_OK &&
                 kui_paint_rotate(p, 0) == KUI_OK &&
                 kui_paint_set_opacity(p, 0.5f) == KUI_OK &&
                 kui_paint_set_opacity(p, 2.0f) == KUI_ERR_INVALID_ARG &&
                 kui_paint_draw_image(p, "missing-image.png", 0, &src, &dest) == KUI_OK &&
                 kui_paint_draw_text(p, "hi", 0, 4, 40, &style, 0x000000FFu) == KUI_OK &&
                 kui_paint_draw_pixels(p, pixels, 2, 2, 30, 30, 8, 8) == KUI_OK &&
                 kui_paint_draw_decoration_line(p, 0, 45, 40, 1, KUI_C_DECORATION_WAVY, 0x00FF00FFu) == KUI_OK &&
                 kui_paint_restore(p) == KUI_OK &&
                 kui_paint_restore(p) == KUI_ERR_INVALID_ARG &&
                 kui_paint_draw_decoration_line(p, 0, 0, 10, 1, (KuiCDecorationStyle)9, 0) == KUI_ERR_INVALID_ARG;
    /* left unbalanced on purpose: KUI restores it after the callback */
    s->restore_unbalanced = kui_paint_save(p) == KUI_OK;
}
static void ce_tick(KuiElement* e, double dt, void* ud) {
    (void)dt;
    ++((CeState*)ud)->ticks;
    kui_element_request_repaint(e); /* animating: keeps ticks on every frame */
}
static int ce_needs_tick(KuiElement* e, void* ud) { (void)e; return ((CeState*)ud)->need_ticks; }
static int ce_key(KuiElement* e, const KuiCCustomKeyEvent* ev, void* ud) {
    CeState* s = (CeState*)ud;
    (void)e;
    if (ev->type == KUI_C_CUSTOM_KEY_DOWN) {
        ++s->keys;
        snprintf(s->key, sizeof s->key, "%.*s", (int)ev->key_len, ev->key);
    }
    return 1;
}
static int ce_mouse(KuiElement* e, const KuiCCustomMouseEvent* ev, void* ud) {
    CeState* s = (CeState*)ud;
    (void)e;
    if (ev->type == KUI_C_CUSTOM_MOUSE_DOWN) {
        ++s->mouse_downs;
        s->mouse_x = ev->x;
        s->mouse_y = ev->y;
        s->mouse_button = ev->button;
    }
    return 0;
}
static int ce_wheel(KuiElement* e, const KuiCCustomWheelEvent* ev, void* ud) {
    CeState* s = (CeState*)ud;
    (void)e;
    ++s->wheels;
    s->wheel_dy = ev->delta_y;
    return 1;
}
static int ce_ime(KuiElement* e, const KuiCCustomImeEvent* ev, void* ud) {
    CeState* s = (CeState*)ud;
    (void)e;
    if (ev->type == KUI_C_IME_UPDATE) {
        ++s->ime_updates;
        snprintf(s->update, sizeof s->update, "%.*s", (int)ev->text_len, ev->text);
    } else if (ev->type == KUI_C_IME_COMMIT) {
        ++s->ime_commits;
        snprintf(s->commit, sizeof s->commit, "%.*s", (int)ev->committed_len, ev->committed_text);
    }
    return 1;
}
static void ce_release(void* ud) { ++((CeState*)ud)->released; }
static void ce_release_other(void* ud) { (void)ud; ++ce_released_other; }

static void test_custom_element_hooks(KuiDocument* doc) {
    KuiCCustomElementDefinition def;
    KuiElement* el = NULL;
    KuiCTextMetrics metrics;
    int paints = 0, ticks = 0;
    memset(&ce, 0, sizeof ce);
    ce.need_ticks = 1;
    ce.mouse_x = ce.mouse_y = -1;
    memset(&def, 0, sizeof def);
    def.callbacks.connected = ce_connected;
    def.callbacks.user_data = &ce;
    def.paint = ce_paint;
    def.tick = ce_tick;
    def.needs_tick = ce_needs_tick;
    def.key = ce_key;
    def.mouse = ce_mouse;
    def.wheel = ce_wheel;
    def.ime = ce_ime;
    def.release_fn = ce_release;
    CHECK_OK(kui_document_define_custom_element_ex(doc, "x-c-canvas", 0, &def), "define_custom_element_ex");
    def.release_fn = ce_release_other;
    CHECK(kui_document_define_custom_element_ex(doc, "x-c-canvas", 0, &def) == KUI_ERR_INVALID_ARG &&
          ce_released_other == 0, "a duplicate tag is rejected and its user_data stays with the caller");

    kui_document_load_html(doc,
        "<html><body style='margin:0'>"
        "<x-c-canvas id='cv' tabindex='0' style='display:block;position:absolute;left:10px;top:10px;"
        "width:120px;height:60px'></x-c-canvas></body></html>", 0);
    for (int i = 0; i < 20 && (ce.paints == 0 || ce.ticks < 3); ++i) kui_platform_wait_events(0.02);
    CHECK(ce.connected == 1, "connected callback still works");
    CHECK(ce.paints > 0, "paint callback runs");
    CHECK(ce.paint_w == 120 && ce.paint_h == 60 && ce.dpr > 0, "paint gets the content-box size");
    CHECK(ce.draw_ok, "drawing calls inside paint");
    CHECK(ce.restore_unbalanced, "an unbalanced save inside paint is allowed");
    CHECK(ce.ticks >= 3, "a tick that requests a repaint keeps ticking every frame");

    ce.need_ticks = 0;
    for (int i = 0; i < 3; ++i) kui_platform_wait_events(0.02);
    ticks = ce.ticks;
    for (int i = 0; i < 5; ++i) kui_platform_wait_events(0.02);
    CHECK(ce.ticks == ticks, "needs_tick 0 stops the ticks");

    kui_document_get_element_by_id(doc, "cv", 0, &el);
    CHECK(el != NULL, "custom element found");
    if (!el) return;
    CHECK_OK(kui_element_measure_text(el, "Hello", 0, NULL, &metrics), "measure_text");
    CHECK(metrics.width > 0 && metrics.ascent > 0 && metrics.line_height > 0, "text metrics");
    paints = ce.paints;
    CHECK_OK(kui_element_request_repaint(el), "request_repaint");
    for (int i = 0; i < 10 && ce.paints == paints; ++i) kui_platform_wait_events(0.02);
    CHECK(ce.paints > paints, "request_repaint paints again");
    paints = ce.paints;
    CHECK_OK(kui_element_invalidate_rect(el, 0, 0, 10, 10), "invalidate_rect");
    for (int i = 0; i < 10 && ce.paints == paints; ++i) kui_platform_wait_events(0.02);
    CHECK(ce.paints > paints, "invalidate_rect paints again");
    CHECK(kui_element_invalidate_rect(el, 0, 0, 0, 10) == KUI_ERR_INVALID_ARG, "empty rect rejected");

    kui_devtools_dispatch_mouse_event(doc, KUI_C_MOUSE_PRESSED, 40, 30, KUI_C_MOUSE_BUTTON_LEFT, 0, 1, 0, 0);
    kui_devtools_dispatch_mouse_event(doc, KUI_C_MOUSE_RELEASED, 40, 30, KUI_C_MOUSE_BUTTON_LEFT, 0, 1, 0, 0);
    CHECK(ce.mouse_downs == 1 && ce.mouse_x == 30 && ce.mouse_y == 20 && ce.mouse_button == 0,
          "mouse callback gets content-box coordinates and the W3C button");
    kui_devtools_dispatch_mouse_event(doc, KUI_C_MOUSE_WHEEL, 40, 30, KUI_C_MOUSE_BUTTON_NONE, 0, 0, 0, 40);
    CHECK(ce.wheels == 1 && ce.wheel_dy == 40, "wheel callback");

    CHECK_OK(kui_element_focus(el), "focus the custom element");
    kui_devtools_dispatch_key_event(doc, KUI_C_KEY_DOWN, "a", 0, "KeyA", 0, 0, "a", 0, 0);
    CHECK(ce.keys == 1 && strcmp(ce.key, "a") == 0, "key callback on the focused element");
    CHECK_OK(kui_devtools_dispatch_ime_event(doc, KUI_C_IME_START, NULL, 0), "ime start");
    CHECK_OK(kui_devtools_dispatch_ime_event(doc, KUI_C_IME_UPDATE, "zhong", 0), "ime update");
    CHECK_OK(kui_devtools_dispatch_ime_event(doc, KUI_C_IME_COMMIT, "\xE4\xB8\xAD", 0), "ime commit");
    CHECK(ce.ime_updates == 1 && strcmp(ce.update, "zhong") == 0, "ime update callback");
    CHECK(ce.ime_commits == 1 && strcmp(ce.commit, "\xE4\xB8\xAD") == 0, "ime commit callback");
    CHECK(kui_devtools_dispatch_ime_event(doc, (KuiCImeEventType)7, NULL, 0) == KUI_ERR_INVALID_ARG,
          "unknown ime step rejected");
    kui_element_release(el);
    CHECK(ce.released == 0, "definition lives with its document");

    /* release_fn runs once when the definition's document goes away */
    {
        KuiDocument* other = NULL;
        CHECK_OK(kui_document_create(&other), "standalone document");
        if (other) {
            memset(&def, 0, sizeof def);
            def.callbacks.user_data = &ce;
            def.tick = ce_tick;
            def.release_fn = ce_release;
            CHECK_OK(kui_document_define_custom_element_ex(other, "x-c-other", 0, &def), "define on a second document");
            kui_document_release(other);
            CHECK(ce.released == 1, "release_fn when the document goes away");
        }
    }
}

/* -- Section 18: components ----------------------------------------------- */
typedef struct CompState {
    int rendered, focus_changes, activated, released, selected, toggles, selection_changes;
    int32_t last_focus;
    uint32_t last_selected, last_toggled;
    int last_expanded;
    size_t last_selection_count;
    char label[32];
} CompState;
static CompState comp;

static KuiElement* comp_render(KuiDocument* d, uint32_t index, void* ud) {
    KuiElement* row = NULL;
    char text[32];
    (void)ud;
    if (kui_document_create_element(d, "div", 0, &row) != KUI_OK) return NULL;
    snprintf(text, sizeof text, "row %u", (unsigned)index);
    kui_element_set_text_content(row, text, 0);
    ++comp.rendered;
    return row;
}
static float comp_height(uint32_t index, void* ud) { (void)ud; return index % 2 ? 30.0f : 10.0f; }
static void comp_focus(int32_t old_index, int32_t new_index, void* ud) {
    (void)old_index; (void)ud;
    ++comp.focus_changes;
    comp.last_focus = new_index;
}
static void comp_index(uint32_t index, void* ud) { (void)ud; ++comp.activated; comp.last_selected = index; }
static void comp_release(void* ud) { (void)ud; ++comp.released; }
static void comp_item(uint32_t index, KuiCListViewItem* item, void* ud) {
    (void)ud;
    snprintf(comp.label, sizeof comp.label, "Item %u", (unsigned)index);
    item->text = comp.label;
    item->subtitle = "sub";
    item->disabled = index == 3;
}
static void comp_select(uint32_t index, void* ud) { (void)ud; ++comp.selected; comp.last_selected = index; }
static void comp_toggle(uint32_t node, int expanded, void* ud) {
    (void)ud;
    ++comp.toggles;
    comp.last_toggled = node;
    comp.last_expanded = expanded;
}
static void comp_selection(const uint32_t* selected, size_t count, void* ud) {
    (void)selected; (void)ud;
    ++comp.selection_changes;
    comp.last_selection_count = count;
}

static int element_text_is(KuiElement* el, const char* expected) {
    char buf[64];
    size_t len = 0;
    return kui_element_get_text_content(el, buf, sizeof buf, &len) == KUI_OK && strcmp(buf, expected) == 0;
}

static void test_components(KuiDocument* doc) {
    int defined = 0;
    memset(&comp, 0, sizeof comp);
    CHECK_OK(kui_document_register_builtin_components(doc), "register built-in components");
    CHECK(kui_document_custom_element_is_defined(doc, "kui-dialog", 0, &defined) == KUI_OK && defined,
          "<kui-dialog> is defined");

    /* themes */
    {
        KuiThemeManager* themes = NULL;
        KuiCKeyValue vars[2] = {{"--brand-bg", 0, "#123456", 0}, {"--brand-fg", 0, "white", 0}};
        size_t count = 0, len = 0, vlen = 0;
        char name[32], value[32], style[256];
        KuiElement* root = NULL;
        CHECK_OK(kui_theme_manager_create(&themes), "theme manager");
        CHECK_OK(kui_theme_manager_get_theme_count(themes, &count), "theme count");
        CHECK(count == 2, "light and dark are built in");
        CHECK_OK(kui_theme_manager_register(themes, "brand", 0, vars, 2), "register a theme");
        CHECK_OK(kui_theme_manager_get_variable_count(themes, "brand", 0, &count), "variable count");
        CHECK(count == 2, "two variables");
        CHECK_OK(kui_theme_manager_get_variable(themes, "brand", 0, 0, name, sizeof name, &len, value,
                                                sizeof value, &vlen), "read a variable");
        CHECK(strncmp(name, "--brand-", 8) == 0 && vlen > 0, "variable name and value");
        CHECK(kui_theme_manager_apply(themes, "missing", 0, doc) == KUI_ERR_NOT_FOUND, "unknown theme");
        kui_document_load_html(doc, "<html><body></body></html>", 0);
        CHECK_OK(kui_theme_manager_apply(themes, "brand", 0, doc), "apply a theme");
        CHECK_OK(kui_theme_manager_get_active(themes, name, sizeof name, &len), "active theme");
        CHECK(strcmp(name, "brand") == 0, "active theme is brand");
        kui_document_root(doc, &root);
        CHECK(root && kui_element_get_style_property(root, "--brand-bg", 0, style, sizeof style, &len) == KUI_OK &&
              strcmp(style, "#123456") == 0, "theme variables land on <html>");
        if (root) kui_element_release(root);
        kui_theme_manager_release(themes);
    }

    /* virtual list */
    kui_document_load_html(doc,
        "<html><body style='margin:0'>"
        "<div id='vl' style='width:200px;height:100px'></div>"
        "<div style='position:absolute;left:300px;top:0'><div id='lv' style='width:200px;height:120px'></div></div>"
        "<div style='position:absolute;left:0;top:200px'><div id='tv' style='width:200px;height:120px'></div></div>"
        "</body></html>", 0);
    for (int i = 0; i < 5; ++i) kui_platform_wait_events(0.01);
    {
        KuiElement* container = NULL;
        KuiVirtualList* list = NULL;
        KuiCVirtualListConfig config;
        KuiCVirtualListState state;
        kui_document_get_element_by_id(doc, "vl", 0, &container);
        memset(&config, 0, sizeof config);
        config.total_items = 1000;
        config.item_height = 20;
        config.overscan = 2;
        config.render_item = comp_render;
        config.on_focus_change = comp_focus;
        config.on_activate = comp_index;
        config.release_fn = comp_release;
        CHECK(kui_virtual_list_create(container, NULL, &list) == KUI_ERR_INVALID_ARG, "config required");
        CHECK_OK(kui_virtual_list_create(container, &config, &list), "virtual list");
        if (list) {
            CHECK_OK(kui_virtual_list_get_state(list, &state), "virtual list state");
            CHECK(comp.rendered > 0 && state.visible_end > 0 && state.visible_end - state.visible_start < 50 &&
                  state.total_items == 1000 && state.total_height == 20000, "only the rows in view are rendered");
            kui_virtual_list_scroll_to_index(list, 500);
            kui_virtual_list_get_state(list, &state);
            CHECK(state.visible_start <= 500 && 500 < state.visible_end, "scroll_to_index");
            kui_virtual_list_set_focused_index(list, 3);
            kui_virtual_list_move_focus(list, 2);
            kui_virtual_list_get_state(list, &state);
            CHECK(comp.focus_changes == 2 && comp.last_focus == 5 && state.focused_index == 5, "focus");
            kui_virtual_list_set_total_items(list, 10);
            CHECK_OK(kui_virtual_list_insert_items(list, 0, 5), "insert items");
            CHECK_OK(kui_virtual_list_remove_items(list, 0, 3), "remove items");
            CHECK(kui_virtual_list_remove_items(list, 10, 5) == KUI_ERR_INVALID_ARG, "remove past the end");
            kui_virtual_list_get_state(list, &state);
            CHECK(state.total_items == 12, "item count after insert and remove");
            CHECK_OK(kui_virtual_list_set_scroll_unit(list, KUI_C_SCROLL_LINE), "scroll unit");
            kui_virtual_list_get_state(list, &state);
            CHECK(state.scroll_unit == KUI_C_SCROLL_LINE, "scroll unit read back");
            CHECK_OK(kui_virtual_list_scroll_to_bottom(list), "scroll to bottom");
            CHECK_OK(kui_virtual_list_refresh(list), "refresh");
            CHECK_OK(kui_virtual_list_release(list), "release virtual list");
            CHECK(comp.released == 1, "virtual list release_fn");
        }
        /* variable heights */
        config.height_for_item = comp_height;
        config.total_items = 4;
        config.release_fn = NULL;
        CHECK_OK(kui_virtual_list_create(container, &config, &list), "variable-height list");
        if (list) {
            kui_virtual_list_get_state(list, &state);
            CHECK(state.total_height == 80, "heights come from height_for_item");
            kui_virtual_list_release(list);
        }
        kui_element_release(container);
    }

    /* list view */
    {
        KuiElement* container = NULL;
        KuiListView* list = NULL;
        KuiCListViewConfig config;
        uint32_t selected[8];
        size_t count = 0;
        int is = 0;
        KuiCVirtualListState state;
        KuiElement* first_row = NULL;
        kui_document_get_element_by_id(doc, "lv", 0, &container);
        memset(&config, 0, sizeof config);
        config.item_count = 100;
        config.item_height = 30;
        config.selection_mode = KUI_C_SELECTION_MULTI;
        config.item_selected_bg = "#ff0000";
        config.item_provider = comp_item;
        config.on_select = comp_select;
        config.release_fn = comp_release;
        CHECK_OK(kui_list_view_create(container, &config, &list), "list view");
        if (list) {
            CHECK_OK(kui_list_view_get_state(list, &state), "list view state");
            CHECK(state.total_items == 100 && state.item_height == 30, "list view geometry");
            kui_element_query_selector(container, "div div div", 0, &first_row);
            CHECK(first_row != NULL, "rows are in the DOM");
            if (first_row) kui_element_release(first_row);
            kui_list_view_select(list, 5);
            kui_list_view_select_range(list, 10, 13);  /* [10, 13) */
            CHECK_OK(kui_list_view_get_selected(list, NULL, 0, &count), "selection count");
            CHECK(count == 4, "multi selection holds 4 rows");
            kui_list_view_get_selected(list, selected, 8, &count);
            CHECK(selected[0] == 5 && selected[3] == 12, "ascending selection");
            kui_list_view_deselect(list, 5);
            kui_list_view_is_selected(list, 5, &is);
            CHECK(!is, "deselect");
            kui_list_view_clear_selection(list);
            kui_list_view_get_selected(list, NULL, 0, &count);
            CHECK(count == 0, "clear selection");
            /* a click selects through the native row handler (after the rows are laid out) */
            for (int i = 0; i < 5; ++i) kui_platform_wait_events(0.01);
            kui_devtools_dispatch_mouse_event(doc, KUI_C_MOUSE_PRESSED, 350, 2 * 30 + 10, KUI_C_MOUSE_BUTTON_LEFT, 0, 1, 0, 0);
            kui_devtools_dispatch_mouse_event(doc, KUI_C_MOUSE_RELEASED, 350, 2 * 30 + 10, KUI_C_MOUSE_BUTTON_LEFT, 0, 1, 0, 0);
            CHECK(comp.selected == 1 && comp.last_selected == 2, "clicking a row calls on_select");
            kui_list_view_is_selected(list, 2, &is);
            CHECK(is, "clicked row is selected");
            CHECK_OK(kui_list_view_set_item_count(list, 20), "set item count");
            CHECK_OK(kui_list_view_scroll_to_bottom(list), "scroll to bottom");
            CHECK_OK(kui_list_view_refresh(list), "refresh");
            kui_list_view_release(list);
            CHECK(comp.released == 2, "list view release_fn");
        }
        kui_element_release(container);
    }

    /* tree view */
    {
        KuiElement* container = NULL;
        KuiTreeView* tree = NULL;
        KuiCTreeViewConfig config;
        KuiCTreeNode node;
        uint32_t a = 0, a1 = 0, a2 = 0, b = 0, child = 0, rows = 0, selected[8];
        size_t count = 0;
        int32_t index = 0;
        int is = 0;
        kui_document_get_element_by_id(doc, "tv", 0, &container);
        CHECK_OK(kui_tree_view_create(&tree), "tree view");
        if (tree) {
            kui_tree_view_add_node(tree, "A", 0, "+", 0, &a);
            kui_tree_view_add_child(tree, a, "A1", 0, NULL, 0, &a1);
            kui_tree_view_add_child(tree, a, "A2", 0, NULL, 0, &a2);
            kui_tree_view_add_node(tree, "B", 0, NULL, 0, &b);
            CHECK(kui_tree_view_add_child(tree, 99, "X", 0, NULL, 0, NULL) == KUI_ERR_INVALID_ARG, "unknown parent");
            CHECK_OK(kui_tree_view_get_node(tree, a, &node), "get node");
            CHECK(node.child_count == 2 && node.parent == -1 && strcmp(node.text, "A") == 0 &&
                  strcmp(node.icon, "+") == 0, "node data");
            kui_tree_view_get_node_child(tree, a, 1, &child);
            CHECK(child == a2, "second child");
            memset(&config, 0, sizeof config);
            config.multi_select = 1;
            config.on_select = comp_select;
            config.on_toggle = comp_toggle;
            config.on_selection_changed = comp_selection;
            config.release_fn = comp_release;
            CHECK_OK(kui_tree_view_setup(tree, container, &config), "tree setup");
            kui_tree_view_get_visible_row_count(tree, &rows);
            CHECK(rows == 2, "collapsed: two root rows");
            kui_tree_view_expand(tree, a);
            kui_tree_view_get_visible_row_count(tree, &rows);
            CHECK(rows == 4 && comp.toggles == 1 && comp.last_toggled == a && comp.last_expanded == 1,
                  "expand shows the children and calls on_toggle");
            comp.selected = 0;
            kui_tree_view_select(tree, a1);
            kui_tree_view_get_selected_index(tree, &index);
            CHECK(comp.selected == 1 && index == (int32_t)a1, "select");
            kui_tree_view_toggle_select(tree, b);
            kui_tree_view_get_selected(tree, selected, 8, &count);
            CHECK(count == 2 && selected[0] == a1 && selected[1] == b && comp.last_selection_count == 2,
                  "toggle_select adds to the selection");
            kui_tree_view_select_all(tree);
            kui_tree_view_get_selected(tree, NULL, 0, &count);
            CHECK(count == 4, "select all");
            kui_tree_view_is_selected(tree, a2, &is);
            CHECK(is, "is_selected");
            kui_tree_view_clear_selection(tree);
            kui_tree_view_get_selected(tree, NULL, 0, &count);
            CHECK(count == 0, "clear selection");
            CHECK_OK(kui_tree_view_set_node(tree, b, "B2", 0, NULL, 0, 1), "set node");
            kui_tree_view_get_node(tree, b, &node);
            CHECK(strcmp(node.text, "B2") == 0 && node.disabled == 1, "node changed");
            kui_tree_view_collapse_all(tree);
            kui_tree_view_get_visible_row_count(tree, &rows);
            CHECK(rows == 2, "collapse all");
            CHECK(kui_tree_view_expand(tree, 42) == KUI_ERR_NOT_FOUND, "unknown node");
            CHECK_OK(kui_tree_view_refresh(tree), "refresh");
            kui_tree_view_release(tree);
            CHECK(comp.released == 3, "tree view release_fn");
        }
        kui_element_release(container);
    }
    (void)element_text_is;
}

/* -- Section 19: document style profile ------------------------------------ */
/* kui_element_get/set_value, checked, selected_index: the control's current state, not the content
 * attribute (the initial value="..."). */
static void test_form_state(KuiDocument* doc) {
    kui_document_load_html(doc,
        "<html><body>"
        "<input id='name' type='text' value='initial'>"
        "<textarea id='bio'>seed</textarea>"
        "<select id='size'><option value='s'>S</option><option value='m' selected>M</option>"
        "<option value='l'>L</option></select>"
        "<input id='agree' type='checkbox'>"
        "<input id='go' type='radio' checked>"
        "<progress id='prog' value='3' max='10'></progress>"
        "<meter id='meter' min='0' max='100' value='25'></meter>"
        "<div id='plain' value='attr'></div>"
        "</body></html>", 0);
    for (int i = 0; i < 5; ++i) kui_platform_wait_events(0.01);
    KuiElement* name = NULL; KuiElement* bio = NULL; KuiElement* size = NULL; KuiElement* agree = NULL;
    KuiElement* radio = NULL; KuiElement* prog = NULL; KuiElement* meter = NULL; KuiElement* plain = NULL;
    kui_document_get_element_by_id(doc, "name", 0, &name);
    kui_document_get_element_by_id(doc, "bio", 0, &bio);
    kui_document_get_element_by_id(doc, "size", 0, &size);
    kui_document_get_element_by_id(doc, "agree", 0, &agree);
    kui_document_get_element_by_id(doc, "go", 0, &radio);
    kui_document_get_element_by_id(doc, "prog", 0, &prog);
    kui_document_get_element_by_id(doc, "meter", 0, &meter);
    kui_document_get_element_by_id(doc, "plain", 0, &plain);
    CHECK(name && bio && size && agree && radio && prog && meter && plain, "form elements found");
    if (!(name && bio && size && agree && radio && prog && meter && plain)) return;

    char buffer[64];
    size_t length = 0;
    /* text input: initial value, then a value set from the host; the content attribute stays at the initial one */
    CHECK_OK(kui_element_get_value(name, buffer, sizeof buffer, &length), "get_value text");
    CHECK(strcmp(buffer, "initial") == 0 && length == 7, "initial value");
    CHECK_OK(kui_element_set_value(name, "Ada", 3), "set_value text");
    CHECK_OK(kui_element_get_value(name, buffer, sizeof buffer, &length), "get_value after set");
    CHECK(strcmp(buffer, "Ada") == 0 && length == 3, "current value is what was set");
    CHECK_OK(kui_element_get_attribute(name, "value", 0, buffer, sizeof buffer, &length), "get_attribute value");
    CHECK(strcmp(buffer, "initial") == 0, "the content attribute still holds the initial value");
    /* two-call length query and truncation, as for every string out-parameter */
    CHECK_OK(kui_element_get_value(name, NULL, 0, &length), "length query");
    CHECK(length == 3, "length query reports 3");
    char tiny[3];
    CHECK(kui_element_get_value(name, tiny, sizeof tiny, &length) == KUI_ERR_BUFFER_TOO_SMALL, "truncated");
    CHECK(length == 3 && strcmp(tiny, "Ad") == 0, "truncated copy is NUL-terminated and out_len is the full length");
    CHECK_OK(kui_element_set_value(name, "", 0), "set empty value");
    kui_element_get_value(name, buffer, sizeof buffer, &length);
    CHECK(length == 0 && buffer[0] == '\0', "empty value");

    /* textarea and select */
    CHECK_OK(kui_element_get_value(bio, buffer, sizeof buffer, &length), "get_value textarea");
    CHECK(strcmp(buffer, "seed") == 0, "textarea initial text");
    CHECK_OK(kui_element_set_value(bio, "line1\nline2", 11), "set_value textarea");
    kui_element_get_value(bio, buffer, sizeof buffer, &length);
    CHECK(strcmp(buffer, "line1\nline2") == 0, "textarea current text");
    CHECK_OK(kui_element_get_value(size, buffer, sizeof buffer, &length), "get_value select");
    CHECK(strcmp(buffer, "m") == 0, "select value is the selected option");
    int32_t index = -9;
    CHECK_OK(kui_element_get_selected_index(size, &index), "get_selected_index");
    CHECK(index == 1, "selected index 1");
    CHECK_OK(kui_element_set_selected_index(size, 2), "set_selected_index");
    kui_element_get_value(size, buffer, sizeof buffer, &length);
    kui_element_get_selected_index(size, &index);
    CHECK(strcmp(buffer, "l") == 0 && index == 2, "selection moved to the third option");
    CHECK_OK(kui_element_set_value(size, "s", 1), "set_value select");
    kui_element_get_selected_index(size, &index);
    CHECK(index == 0, "setting the select value picks the matching option");

    /* checked: checkbox starts unchecked, radio has the checked attribute */
    int checked = -1;
    CHECK_OK(kui_element_get_checked(agree, &checked), "get_checked checkbox");
    CHECK(checked == 0, "checkbox unchecked");
    CHECK_OK(kui_element_set_checked(agree, 1), "set_checked");
    kui_element_get_checked(agree, &checked);
    CHECK(checked == 1, "checkbox checked");
    CHECK_OK(kui_element_set_checked(agree, 0), "uncheck");
    kui_element_get_checked(agree, &checked);
    CHECK(checked == 0, "checkbox unchecked again");
    kui_element_get_checked(radio, &checked);
    CHECK(checked == 1, "radio starts checked from the attribute");

    /* progress and meter: numeric value as text */
    CHECK_OK(kui_element_get_value(prog, buffer, sizeof buffer, &length), "get_value progress");
    CHECK(strcmp(buffer, "3") == 0, "progress value 3");
    CHECK_OK(kui_element_set_value(prog, "7.5", 3), "set_value progress");
    kui_element_get_value(prog, buffer, sizeof buffer, &length);
    CHECK(strcmp(buffer, "7.5") == 0, "progress value 7.5");
    CHECK(kui_element_set_value(prog, "abc", 3) == KUI_ERR_INVALID_ARG, "non-numeric progress value rejected");
    CHECK_OK(kui_element_get_value(meter, buffer, sizeof buffer, &length), "get_value meter");
    CHECK(strcmp(buffer, "25") == 0, "meter value 25");
    CHECK_OK(kui_element_set_value(meter, "60", 2), "set_value meter");
    kui_element_get_value(meter, buffer, sizeof buffer, &length);
    CHECK(strcmp(buffer, "60") == 0, "meter value 60");

    /* everything else: KUI_ERR_NOT_SUPPORTED, bad arguments: KUI_ERR_INVALID_ARG */
    CHECK(kui_element_get_value(plain, buffer, sizeof buffer, &length) == KUI_ERR_NOT_SUPPORTED, "div has no value state");
    CHECK(kui_element_set_value(plain, "x", 1) == KUI_ERR_NOT_SUPPORTED, "div value not settable");
    checked = -1;
    CHECK(kui_element_get_checked(name, &checked) == KUI_OK && checked == 0, "any <input> has a checked state");
    CHECK(kui_element_get_selected_index(name, &index) == KUI_ERR_NOT_SUPPORTED, "text input has no selected index");
    CHECK(kui_element_get_checked(plain, &checked) == KUI_ERR_NOT_SUPPORTED, "div has no checked state");
    CHECK(kui_element_set_selected_index(plain, 0) == KUI_ERR_NOT_SUPPORTED, "div has no selected index");
    CHECK(kui_element_get_value(NULL, buffer, sizeof buffer, &length) == KUI_ERR_INVALID_ARG, "NULL element");
    CHECK(kui_element_set_value(name, NULL, 4) == KUI_ERR_INVALID_ARG, "NULL value with a length");
    CHECK(kui_element_get_checked(agree, NULL) == KUI_ERR_INVALID_ARG, "NULL out_checked");
    CHECK(kui_element_get_selected_index(size, NULL) == KUI_ERR_INVALID_ARG, "NULL out_index");

    kui_element_release(name); kui_element_release(bio); kui_element_release(size);
    kui_element_release(agree); kui_element_release(radio); kui_element_release(prog);
    kui_element_release(meter); kui_element_release(plain);
}

/* Host integration additions: scroll into view / momentum, wheel delta, script timers, base path,
 * render DPR, window options at creation, enable / disable, one frame now, DevTools token switch,
 * IME, console and crash diagnostics. */
static float g_wheel_dx = -1.0f, g_wheel_dy = -1.0f;
static KuiStatus g_wheel_status = KUI_ERR_INTERNAL;
static KuiStatus g_down_wheel_status = KUI_OK;
static void on_wheel(KuiEvent* event, void* user_data) {
    (void)user_data;
    g_wheel_status = kui_event_wheel_get_delta(event, &g_wheel_dx, &g_wheel_dy);
}
static void on_down(KuiEvent* event, void* user_data) {
    float x = 0, y = 0;
    (void)user_data;
    g_down_wheel_status = kui_event_wheel_get_delta(event, &x, &y);
}

/* capture-phase listeners: kui_element_add_event_listener_ex(..., capture) */
static char g_capture_log[128];
static void log_tag(const char* tag) {
    size_t used = strlen(g_capture_log);
    if (used + strlen(tag) + 2 < sizeof g_capture_log) {
        if (used) g_capture_log[used++] = ' ';
        strcpy(g_capture_log + used, tag);
    }
}
static void on_probe(KuiEvent* event, void* user_data) {
    (void)event;
    log_tag((const char*)user_data);
}
static void on_probe_stop(KuiEvent* event, void* user_data) {
    log_tag((const char*)user_data);
    kui_event_stop_propagation(event);
}
static void test_capture_listeners(KuiDocument* doc) {
    kui_document_load_html(doc,
        "<html><body><div id='outer'><button id='inner'>x</button></div></body></html>", 0);
    for (int i = 0; i < 3; ++i) kui_platform_wait_events(0.01);
    KuiElement* outer = NULL;
    KuiElement* inner = NULL;
    kui_document_get_element_by_id(doc, "outer", 0, &outer);
    kui_document_get_element_by_id(doc, "inner", 0, &inner);
    CHECK(outer && inner, "capture fixture found");
    if (!(outer && inner)) return;

    KuiListenerId outer_capture = 0, outer_plain = 0, inner_capture = 0, inner_plain = 0;
    CHECK_OK(kui_element_add_event_listener(outer, "probe", 0, on_probe, "op", &outer_plain), "outer plain");
    CHECK_OK(kui_element_add_event_listener(inner, "probe", 0, on_probe, "ip", &inner_plain), "inner plain");
    CHECK_OK(kui_element_add_event_listener_ex(inner, "probe", 0, on_probe, "ic", 1, &inner_capture), "inner capture");
    CHECK_OK(kui_element_add_event_listener_ex(outer, "probe", 0, on_probe, "oc", 1, &outer_capture), "outer capture");
    CHECK(kui_element_add_event_listener_ex(NULL, "probe", 0, on_probe, NULL, 1, &outer_capture) == KUI_ERR_INVALID_ARG,
          "NULL element rejected");
    int not_canceled = 0;
    g_capture_log[0] = '\0';
    CHECK_OK(kui_element_dispatch_event(inner, "probe", 0, 1, 1, &not_canceled), "dispatch probe");
    CHECK(strcmp(g_capture_log, "oc ic ip op") == 0, "capture descends first, then target, then bubbling");

    /* a non-capture add_event_listener_ex is the plain listener */
    KuiListenerId plain_ex = 0;
    CHECK_OK(kui_element_add_event_listener_ex(inner, "probe", 0, on_probe, "ix", 0, &plain_ex), "ex with capture = 0");
    g_capture_log[0] = '\0';
    kui_element_dispatch_event(inner, "probe", 0, 1, 1, &not_canceled);
    CHECK(strcmp(g_capture_log, "oc ic ip ix op") == 0, "capture = 0 registers a plain listener");
    kui_element_remove_event_listener(inner, "probe", 0, plain_ex);

    /* removal by id works for a capture listener */
    CHECK_OK(kui_element_remove_event_listener(outer, "probe", 0, outer_capture), "remove capture listener");
    g_capture_log[0] = '\0';
    kui_element_dispatch_event(inner, "probe", 0, 1, 1, &not_canceled);
    CHECK(strcmp(g_capture_log, "ic ip op") == 0, "a removed capture listener no longer runs");

    /* stopPropagation in a capture listener ends the descent */
    KuiListenerId stopper = 0;
    CHECK_OK(kui_element_add_event_listener_ex(outer, "probe", 0, on_probe_stop, "os", 1, &stopper), "stopping capture");
    g_capture_log[0] = '\0';
    kui_element_dispatch_event(inner, "probe", 0, 1, 1, &not_canceled);
    CHECK(strcmp(g_capture_log, "os") == 0, "stopPropagation in capture stops everything below");
    kui_element_remove_event_listener(outer, "probe", 0, stopper);
    kui_element_remove_event_listener(outer, "probe", 0, outer_plain);
    kui_element_remove_event_listener(inner, "probe", 0, inner_plain);
    kui_element_remove_event_listener(inner, "probe", 0, inner_capture);
    kui_element_release(outer);
    kui_element_release(inner);
}

static void test_host_integration(KuiWindow* window, KuiDocument* doc) {
    kui_document_load_html(doc,
        "<html><body style='margin:0'>"
        "<div id='box' style='width:200px;height:100px;overflow:auto'>"
        "<div style='height:1000px'></div><p id='end' style='margin:0;height:20px'>end</p></div>"
        "</body></html>", 0);
    for (int i = 0; i < 5; ++i) kui_platform_wait_events(0.01);
    KuiElement* box = NULL;
    KuiElement* end = NULL;
    kui_document_get_element_by_id(doc, "box", 0, &box);
    kui_document_get_element_by_id(doc, "end", 0, &end);
    CHECK(box && end, "scroll fixture found");
    if (!(box && end)) return;

    /* scroll into view */
    float left = -1, top = -1, width = 0, height = 0;
    CHECK_OK(kui_element_scroll_into_view(end, 1), "scroll_into_view");
    kui_element_get_scroll_metrics(box, &left, &top, &width, &height);
    CHECK(top > 500.0f, "the container scrolled down to the last paragraph");
    kui_element_set_scroll_position(box, 0, 0);
    CHECK(kui_element_scroll_into_view(NULL, 1) == KUI_ERR_INVALID_ARG, "NULL element rejected");

    /* momentum scroll: advances over the next frames */
    CHECK_OK(kui_element_start_momentum_scroll(box, 0.0f, 0.5f), "start_momentum_scroll");
    for (int i = 0; i < 20; ++i) kui_platform_wait_events(0.02);
    kui_element_get_scroll_metrics(box, &left, &top, &width, &height);
    CHECK(top > 0.0f, "momentum moved the container");
    kui_element_set_scroll_position(box, 0, 0);

    /* wheel delta: only a wheel event has one */
    KuiListenerId wheel_id = 0, down_id = 0;
    CHECK_OK(kui_element_add_event_listener(box, "wheel", 0, on_wheel, NULL, &wheel_id), "wheel listener");
    CHECK_OK(kui_element_add_event_listener(box, "mousedown", 0, on_down, NULL, &down_id), "mousedown listener");
    CHECK_OK(kui_devtools_dispatch_mouse_event(doc, KUI_C_MOUSE_WHEEL, 50.0f, 50.0f, KUI_C_MOUSE_BUTTON_NONE, 0, 0,
                                               3.0f, -7.5f), "inject wheel");
    CHECK(g_wheel_status == KUI_OK, "wheel_get_delta on a wheel event");
    CHECK(g_wheel_dx == 3.0f && g_wheel_dy == -7.5f, "wheel delta values");
    CHECK_OK(kui_devtools_dispatch_mouse_event(doc, KUI_C_MOUSE_PRESSED, 50.0f, 50.0f, KUI_C_MOUSE_BUTTON_LEFT, 0, 1,
                                               0.0f, 0.0f), "inject mousedown");
    CHECK(g_down_wheel_status == KUI_ERR_NOT_SUPPORTED, "wheel_get_delta on a mousedown is not supported");
    kui_devtools_dispatch_mouse_event(doc, KUI_C_MOUSE_RELEASED, 50.0f, 50.0f, KUI_C_MOUSE_BUTTON_LEFT, 0, 1, 0.0f, 0.0f);
    CHECK(kui_event_wheel_get_delta(NULL, &left, &top) == KUI_ERR_INVALID_ARG, "NULL event rejected");
    kui_element_remove_event_listener(box, "wheel", 0, wheel_id);
    kui_element_remove_event_listener(box, "mousedown", 0, down_id);

    /* base path and render DPR */
    char path[256];
    size_t path_len = 0;
    CHECK_OK(kui_document_set_base_path(doc, "assets/pages/", 0), "set_base_path");
    CHECK_OK(kui_document_get_base_path(doc, path, sizeof path, &path_len), "get_base_path");
    CHECK(strcmp(path, "assets/pages/") == 0 && path_len == 13, "base path read back");
    CHECK(kui_document_set_base_path(doc, NULL, 0) == KUI_ERR_INVALID_ARG, "NULL base path rejected");
    kui_document_set_base_path(doc, "", 0);
    float ratio = 0.0f;
    kui_window_get_device_pixel_ratio(window, &ratio);
    CHECK_OK(kui_document_set_render_device_pixel_ratio(doc, ratio), "set_render_device_pixel_ratio to its own value");
    CHECK(kui_document_set_render_device_pixel_ratio(doc, 0.0f) == KUI_ERR_INVALID_ARG, "a zero ratio is rejected");
    KuiDocument* headless = NULL;
    CHECK_OK(kui_document_create(&headless), "headless document");
    if (headless) {
        CHECK(kui_document_set_render_device_pixel_ratio(headless, 1.0f) == KUI_ERR_NOT_SUPPORTED,
              "no renderer without a window");
        kui_document_release(headless);
    }

    /* script timers: the engine runs due timers only when the host ticks */
    KuiScriptEngine* engine = NULL;
    if (kui_script_engine_create(&engine) == KUI_OK && engine) {
        int success = 0;
        KuiCScriptValue value;
        kui_script_engine_bind_document(engine, doc);
        memset(&value, 0, sizeof value);
        CHECK_OK(kui_script_engine_eval_value(engine, "host_timer_fired = false; setTimeout(function() host_timer_fired = true end, 50)",
                                              0, "test", 0, &success, &value), "schedule a timer");
        CHECK(success == 1, "timer script ran");
        CHECK_OK(kui_script_engine_tick_timers(engine, 0.0), "tick before the timer is due");
        memset(&value, 0, sizeof value);
        kui_script_engine_eval_value(engine, "return tostring(host_timer_fired)", 0, "test", 0, &success, &value);
        CHECK(value.string_value && strcmp(value.string_value, "false") == 0, "the timer has not fired yet");
        CHECK_OK(kui_script_engine_tick_timers(engine, 100.0), "tick after the timer is due");
        memset(&value, 0, sizeof value);
        kui_script_engine_eval_value(engine, "return tostring(host_timer_fired)", 0, "test", 0, &success, &value);
        CHECK(value.string_value && strcmp(value.string_value, "true") == 0, "the timer fired on the later tick");
        CHECK(kui_script_engine_tick_timers(NULL, 0.0) == KUI_ERR_INVALID_ARG, "NULL engine rejected");
        kui_script_engine_unbind_document(engine);
        kui_script_engine_release(engine);
    }

    /* a window created with the options KuiWindowProps does not have */
    KuiWindowPropsEx ex;
    KuiWindow* hidden = NULL;
    memset(&ex, 0, sizeof ex);
    ex.struct_size = sizeof ex;
    ex.base.title = "kui-c-ex";
    ex.base.width = 320;
    ex.base.height = 200;
    ex.hidden = 1;
    ex.sizeable = 1;
    ex.has_frame_radius = 1;
    ex.frame_radius = 8.0f;
    CHECK_OK(kui_window_create_ex(&ex, &hidden), "create_ex");
    CHECK(hidden != NULL, "create_ex returned a window");
    if (hidden) {
        int visible = -1, started = 0, enabled = -1;
        KuiStatus status;
        CHECK_OK(kui_window_is_visible(hidden, &visible), "is_visible");
        CHECK(visible == 0, "hidden = 1 creates the window without showing it");
        CHECK_OK(kui_window_process_frame(hidden), "process_frame renders the hidden window's first frame");
        CHECK_OK(kui_window_show(hidden), "show");
        kui_window_is_visible(hidden, &visible);
        CHECK(visible == 1, "shown after show");

        status = kui_window_is_enabled(hidden, &enabled);
#ifdef _WIN32
        CHECK(status == KUI_OK && enabled == 1, "a new window accepts input");
        CHECK_OK(kui_window_set_enabled(hidden, 0), "set_enabled(0)");
        kui_window_is_enabled(hidden, &enabled);
        CHECK(enabled == 0, "disabled");
        CHECK_OK(kui_window_set_enabled(hidden, 1), "set_enabled(1)");
        kui_window_is_enabled(hidden, &enabled);
        CHECK(enabled == 1, "enabled again");
#else
        CHECK(status == KUI_ERR_NOT_SUPPORTED, "enable / disable is Windows only");
        CHECK(kui_window_set_enabled(hidden, 0) == KUI_ERR_NOT_SUPPORTED, "set_enabled is Windows only");
#endif
        CHECK(kui_window_set_devtools_token_auth(hidden, 0) == KUI_ERR_NOT_FOUND, "token switch needs a running server");
        CHECK_OK(kui_window_start_devtools(hidden, 0, &started), "start_devtools");
        if (started) {
            CHECK_OK(kui_window_set_devtools_token_auth(hidden, 0), "token check off");
            CHECK_OK(kui_window_set_devtools_token_auth(hidden, 1), "token check on");
            kui_window_stop_devtools(hidden);
        }
        kui_window_release(hidden);
    }
    ex.struct_size = sizeof ex - 1;
    hidden = NULL;
    CHECK(kui_window_create_ex(&ex, &hidden) == KUI_ERR_INVALID_ARG && hidden == NULL, "a short struct_size is rejected");
    CHECK(kui_window_create_ex(NULL, &hidden) == KUI_ERR_INVALID_ARG, "NULL props rejected");

    /* the system input method */
    {
        KuiStatus ime = kui_platform_disable_ime();
        CHECK(ime == KUI_OK || ime == KUI_ERR_NOT_SUPPORTED, "disable_ime");
    }

    /* console and crash diagnostics */
    CHECK_OK(kui_console_set_enabled(1), "console enabled");
    CHECK_OK(kui_console_set_stderr_output(0), "console stderr output off");
    CHECK_OK(kui_console_set_min_level(KUI_C_LOG_DEBUG), "console min level");
    CHECK(kui_console_set_min_level((KuiCLogLevel)9) == KUI_ERR_INVALID_ARG, "an unknown log level is rejected");
    {
        KuiCStackTraceConfig stack;
        KuiCCrashReporterConfig crash;
        int installed = -1;
        memset(&stack, 0, sizeof stack);
        stack.struct_size = sizeof stack;
        stack.output_mode = KUI_C_STACK_TRACE_STDERR;
        CHECK_OK(kui_stack_trace_install(&stack, &installed), "stack_trace_install");
        CHECK(installed == 0 || installed == 1, "install reports whether the writer is installed");
        CHECK_OK(kui_stack_trace_uninstall(), "stack_trace_uninstall");
        stack.struct_size = 1;
        CHECK(kui_stack_trace_install(&stack, &installed) == KUI_ERR_INVALID_ARG, "a short stack trace config is rejected");
        CHECK_OK(kui_stack_trace_install(NULL, &installed), "NULL config uses the defaults");
        kui_stack_trace_uninstall();
        memset(&crash, 0, sizeof crash);
        crash.struct_size = sizeof crash;
        installed = -1;
        CHECK_OK(kui_crash_reporter_install(&crash, &installed), "crash_reporter_install (handler may be missing)");
        CHECK(installed == 0 || installed == 1, "install reports whether the handler started");
        CHECK_OK(kui_crash_reporter_uninstall(), "crash_reporter_uninstall");
    }

    kui_element_release(box);
    kui_element_release(end);
}

static void test_style_profile(KuiDocument* d) {
    KuiElement* body = NULL;
    KuiCStyleProfile profile = KUI_C_STYLE_BROWSER_COMPAT;
    char native_font[128], browser_font[128];
    size_t len = 0;
    CHECK_OK(kui_document_get_style_profile(d, &profile), "get style profile");
    CHECK(profile == KUI_C_STYLE_NATIVE_GUI, "NativeGui is the default");
    kui_document_load_html(d, "<html><body><p>x</p></body></html>", 0);
    for (int i = 0; i < 3; ++i) kui_platform_wait_events(0.01);
    kui_document_body(d, &body);
    native_font[0] = browser_font[0] = '\0';
    if (body) kui_element_get_computed_style_value(body, "font-family", 0, native_font, sizeof native_font, &len);
    CHECK_OK(kui_document_set_style_profile(d, KUI_C_STYLE_BROWSER_COMPAT), "set BrowserCompat");
    kui_document_get_style_profile(d, &profile);
    CHECK(profile == KUI_C_STYLE_BROWSER_COMPAT, "BrowserCompat read back");
    for (int i = 0; i < 3; ++i) kui_platform_wait_events(0.01);
    if (body) kui_element_get_computed_style_value(body, "font-family", 0, browser_font, sizeof browser_font, &len);
    CHECK(strstr(browser_font, "serif") != NULL && strcmp(native_font, browser_font) != 0,
          "BrowserCompat switches the default font to serif");
    CHECK(kui_document_set_style_profile(d, (KuiCStyleProfile)5) == KUI_ERR_INVALID_ARG, "unknown profile");
    kui_document_set_style_profile(d, KUI_C_STYLE_NATIVE_GUI);
    if (body) kui_element_release(body);
}

int main(void) {
    CHECK_OK(kui_platform_init(), "platform init");
    KuiWindowProps props;
    memset(&props, 0, sizeof props);
    props.title = "kui-c-widgets";
    props.width = 640;
    props.height = 480;
    KuiWindow* window = NULL;
    CHECK_OK(kui_window_create(&props, &window), "create window");
    if (window) {
        KuiDocument* doc = NULL;
        kui_window_get_document(window, &doc);
        kui_document_load_html(doc, "<html><body><p>popups</p></body></html>", 0);
        for (int i = 0; i < 5; ++i) kui_platform_wait_events(0.01);
        test_popups(doc);
        test_picker(doc);
        test_audio(doc);
        test_drag_drop(doc);
        test_declarative_context_menu(doc);
        test_custom_element_hooks(doc);
        test_components(doc);
        test_style_profile(doc);
        test_form_state(doc);
        test_host_integration(window, doc);
        test_capture_listeners(doc);
        kui_document_release(doc);
        CHECK_OK(kui_window_release(window), "release window");
    }
    kui_platform_shutdown();
    printf("kui_c widgets: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
