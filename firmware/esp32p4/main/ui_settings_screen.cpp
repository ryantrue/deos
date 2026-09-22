// SPDX-License-Identifier: Apache-2.0

#include "ui_settings_screen.hpp"

#include "ui_theme.hpp"
#include "ui_widgets.hpp"

namespace deos::ui {
namespace {

void bind(lv_obj_t* object, lv_event_cb_t callback, void* user_data) {
    if (callback != nullptr) {
        lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, user_data);
    }
}

}  // namespace

void build_settings_screen(lv_obj_t* screen,
                           const SettingsState& state,
                           const SettingsCallbacks& callbacks) {
    lv_obj_t* body = lv_obj_create(screen);
    lv_obj_set_pos(body, theme::kMargin, theme::kHeaderHeight);
    lv_obj_set_size(body, 672, 574);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 10, 0);
    lv_obj_set_style_pad_row(body, 10, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* network =
        widgets::settings_row(body, "Network", "Wi-Fi and local control plane");
    bind(network, callbacks.network, callbacks.user_data);

    lv_obj_t* display =
        widgets::settings_row(body, "Display", "Brightness and screen behavior");
    bind(display, callbacks.display, callbacks.user_data);

    lv_obj_t* storage =
        widgets::settings_row(body, "Storage", "SD card and DEOS volume");
    bind(storage, callbacks.storage, callbacks.user_data);

    lv_obj_t* update =
        widgets::settings_row(body, "Software Update", "A/B OTA and build status");
    bind(update, callbacks.update, callbacks.user_data);

    if (state.developer_mode) {
        lv_obj_t* developer =
            widgets::settings_row(body, "Developer", "Diagnostics and debug tools");
        bind(developer, callbacks.developer, callbacks.user_data);
    }

    lv_obj_t* about =
        widgets::settings_row(body, "About DEOS", "System, build and hardware");
    bind(about, callbacks.system, callbacks.user_data);

    lv_obj_t* note = widgets::label(
        body,
        state.developer_mode
            ? "Developer Mode is enabled and persists across reboot."
            : "Developer controls stay hidden during normal use.",
        theme::color(0x606B77));
    lv_obj_set_style_pad_top(note, 8, 0);
}

}  // namespace deos::ui
