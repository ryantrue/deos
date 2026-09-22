// SPDX-License-Identifier: Apache-2.0

#include "ui_home_screen.hpp"

#include "ui_theme.hpp"
#include "ui_widgets.hpp"

namespace deos::ui {
namespace {

using theme::color;
using widgets::label;
using widgets::tile;

void bind(lv_obj_t* object, lv_event_cb_t callback, void* user_data) {
    if (callback != nullptr) {
        lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, user_data);
    }
}

}  // namespace

HomeView build_home_screen(lv_obj_t* screen,
                           const HomeState& state,
                           const HomeCallbacks& callbacks) {
    HomeView view{};

    lv_obj_t* intro = label(screen, "Device status and controls", color(0x75808D));
    lv_obj_set_pos(intro, theme::kMargin, 72);

    constexpr int col = 216;
    constexpr int wide = 444;

    lv_obj_t* system = tile(
        screen, theme::kMargin, 112, 672, "This device", "Local system status",
        color(0x14243B), color(0x285887));
    view.system_value =
        label(system, "STARTING", color(0xE1C777), &lv_font_montserrat_20);
    lv_obj_set_pos(view.system_value, 0, 84);
    bind(system, callbacks.system, callbacks.user_data);

    lv_obj_t* settings = tile(
        screen, theme::kMargin, 270, wide, "Settings", "Network, display and storage",
        color(0x17251F), color(0x28573F));
    view.settings_value = label(settings, "Loading device state...", color(0x9EE2BB));
    lv_obj_set_pos(view.settings_value, 0, 84);
    bind(settings, callbacks.settings, callbacks.user_data);

    lv_obj_t* storage = tile(
        screen, 480, 270, col, "Storage", "Internal and SD",
        color(0x202117), color(0x55562A));
    view.storage_value = label(storage, "unknown", color(0xA8A570));
    lv_obj_set_pos(view.storage_value, 0, 84);
    bind(storage, callbacks.storage, callbacks.user_data);

    lv_obj_t* control = tile(
        screen, theme::kMargin, 428, col, "Control", "State and actions",
        color(0x171B21), color(0x2A3039));
    view.control_value = label(control, state.model_count.c_str(), color(0xA9B1BA));
    lv_obj_set_pos(view.control_value, 0, 84);
    bind(control, callbacks.control, callbacks.user_data);

    lv_obj_t* apps = tile(
        screen, 252, 428, col, "Apps", "Device applications",
        color(0x1D1C1B), color(0x343330));
    lv_obj_t* app_count = label(apps, "Open app list", color(0xA9B1BA));
    lv_obj_set_pos(app_count, 0, 84);
    bind(apps, callbacks.apps, callbacks.user_data);

    lv_obj_t* ai = tile(
        screen, 480, 428, col, "AI", "Optional service",
        color(0x171B21), color(0x2A3039));
    lv_obj_t* ai_hint = label(ai, "Not configured", color(0x8C97A4));
    lv_obj_set_pos(ai_hint, 0, 84);
    bind(ai, callbacks.ai, callbacks.user_data);

    lv_obj_t* dock = lv_obj_create(screen);
    lv_obj_set_pos(dock, theme::kMargin, 610);
    lv_obj_set_size(dock, 672, 82);
    lv_obj_remove_flag(dock, LV_OBJ_FLAG_SCROLLABLE);
    theme::apply_panel(dock, color(0x10141A), color(0x242A32));
    lv_obj_set_style_pad_all(dock, 10, 0);

    const char* dock_labels[] = {"HOME", "CONTROL", "APPS", "SETTINGS"};
    const lv_event_cb_t dock_callbacks[] = {
        callbacks.home, callbacks.control, callbacks.apps, callbacks.settings};
    for (int i = 0; i < 4; ++i) {
        lv_obj_t* item = lv_button_create(dock);
        lv_obj_set_pos(item, i * 160, 0);
        lv_obj_set_size(item, 150, 60);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_shadow_width(item, 0, 0);
        lv_obj_t* text = label(
            item, dock_labels[i], i == 0 ? color(0xF5F7FA) : color(0x697481));
        lv_obj_center(text);
        bind(item, dock_callbacks[i], callbacks.user_data);
    }

    update_home_screen(view, state);
    return view;
}

void update_home_screen(const HomeView& view, const HomeState& state) {
    if (view.system_value == nullptr || view.storage_value == nullptr ||
        view.control_value == nullptr || view.settings_value == nullptr) {
        return;
    }

    lv_label_set_text(view.system_value, state.ready ? "READY" : "STARTING");
    lv_obj_set_style_text_color(
        view.system_value, state.ready ? color(0x71D99C) : color(0xE1C777), 0);

    lv_label_set_text(view.storage_value, state.storage_state.c_str());
    lv_obj_set_style_text_color(
        view.storage_value,
        state.storage_state == "ready" ? color(0xD8D68A) : color(0xA8A570),
        0);

    lv_label_set_text(view.control_value, state.model_count.c_str());
    lv_label_set_text(view.settings_value, state.connectivity.c_str());
}

}  // namespace deos::ui
