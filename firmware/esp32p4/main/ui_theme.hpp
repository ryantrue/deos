// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "lvgl.h"

#include <cstdint>

namespace deos::ui::theme {

inline constexpr int kScreen = 720;
inline constexpr int kMargin = 24;
inline constexpr int kGap = 12;
inline constexpr int kHeaderHeight = 104;
inline constexpr int kTileHeight = 146;
inline constexpr int kRadius = 24;

lv_color_t color(std::uint32_t rgb) noexcept;
void apply_panel(lv_obj_t* obj,
                 lv_color_t background,
                 lv_color_t border);

}  // namespace deos::ui::theme
