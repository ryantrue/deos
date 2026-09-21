// SPDX-License-Identifier: Apache-2.0

#include "ui_theme.hpp"

namespace deos::ui::theme {

lv_color_t color(std::uint32_t rgb) noexcept {
    return lv_color_hex(rgb);
}

void apply_panel(lv_obj_t* obj, lv_color_t background, lv_color_t border) {
    lv_obj_set_style_bg_color(obj, background, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, border, 0);
    lv_obj_set_style_radius(obj, kRadius, 0);
    lv_obj_set_style_pad_all(obj, 18, 0);
}

}  // namespace deos::ui::theme
