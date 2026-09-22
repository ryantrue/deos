// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "lvgl.h"

namespace deos::ui {

struct SettingsState {
    bool developer_mode{false};
};

struct SettingsCallbacks {
    lv_event_cb_t network{nullptr};
    lv_event_cb_t display{nullptr};
    lv_event_cb_t storage{nullptr};
    lv_event_cb_t update{nullptr};
    lv_event_cb_t developer{nullptr};
    lv_event_cb_t system{nullptr};
    void* user_data{nullptr};
};

void build_settings_screen(lv_obj_t* screen,
                           const SettingsState& state,
                           const SettingsCallbacks& callbacks);

}  // namespace deos::ui
