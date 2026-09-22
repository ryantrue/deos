// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "lvgl.h"

#include <cstddef>
#include <string>

namespace deos::ui {

struct HomeState {
    bool ready{false};
    std::string storage_state{"unknown"};
    std::string connectivity{"Offline"};
    std::string model_count;
};

struct HomeView {
    lv_obj_t* system_value{nullptr};
    lv_obj_t* storage_value{nullptr};
    lv_obj_t* control_value{nullptr};
    lv_obj_t* settings_value{nullptr};
};

struct HomeCallbacks {
    lv_event_cb_t home{nullptr};
    lv_event_cb_t control{nullptr};
    lv_event_cb_t apps{nullptr};
    lv_event_cb_t settings{nullptr};
    lv_event_cb_t system{nullptr};
    lv_event_cb_t storage{nullptr};
    lv_event_cb_t ai{nullptr};
    void* user_data{nullptr};
};

HomeView build_home_screen(lv_obj_t* screen,
                           const HomeState& state,
                           const HomeCallbacks& callbacks);

void update_home_screen(const HomeView& view, const HomeState& state);

}  // namespace deos::ui
