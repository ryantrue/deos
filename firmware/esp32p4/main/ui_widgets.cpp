// SPDX-License-Identifier: Apache-2.0

#include "ui_widgets.hpp"

#include "ui_theme.hpp"

namespace deos::ui::widgets {

lv_obj_t* label(lv_obj_t* parent,
                const char* text,
                lv_color_t text_color,
                const lv_font_t* font) {
    lv_obj_t* obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_color(obj, text_color, 0);
    lv_obj_set_style_text_font(obj, font, 0);
    return obj;
}

lv_obj_t* tile(lv_obj_t* parent,
               int x,
               int y,
               int width,
               const char* title,
               const char* subtitle,
               lv_color_t background,
               lv_color_t border) {
    lv_obj_t* obj = lv_button_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, theme::kTileHeight);
    theme::apply_panel(obj, background, border);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_transform_scale(obj, 248, LV_STATE_PRESSED);

    lv_obj_t* title_label =
        label(obj, title, theme::color(0xF5F7FA), &lv_font_montserrat_20);
    lv_obj_set_pos(title_label, 0, 0);

    lv_obj_t* subtitle_label = label(obj, subtitle, theme::color(0x8E99A6));
    lv_obj_set_pos(subtitle_label, 0, 36);
    return obj;
}

lv_obj_t* settings_row(lv_obj_t* parent,
                       const char* title,
                       const char* subtitle,
                       bool enabled) {
    lv_obj_t* row = enabled ? lv_button_create(parent) : lv_obj_create(parent);
    lv_obj_set_width(row, 652);
    lv_obj_set_height(row, 86);
    theme::apply_panel(row, theme::color(0x12161C), theme::color(0x262C34));
    lv_obj_set_style_pad_all(row, 14, 0);
    if (enabled) {
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_transform_scale(row, 250, LV_STATE_PRESSED);
    }

    lv_obj_t* title_label = label(
        row, title,
        enabled ? theme::color(0xF5F7FA) : theme::color(0x6E7884),
        &lv_font_montserrat_20);
    lv_obj_set_pos(title_label, 0, 0);

    lv_obj_t* subtitle_label = label(
        row, subtitle,
        enabled ? theme::color(0x87929F) : theme::color(0x555E69));
    lv_obj_set_pos(subtitle_label, 0, 34);

    if (enabled) {
        lv_obj_t* chevron =
            label(row, ">", theme::color(0x626D79), &lv_font_montserrat_20);
        lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    return row;
}

lv_obj_t* action(lv_obj_t* parent,
                 const char* text,
                 lv_color_t background,
                 lv_color_t foreground,
                 int width) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_size(button, width, 58);
    lv_obj_set_style_bg_color(button, background, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 20, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_transform_scale(button, 248, LV_STATE_PRESSED);
    lv_obj_t* text_label = label(button, text, foreground, &lv_font_montserrat_20);
    lv_obj_center(text_label);
    return button;
}

}  // namespace deos::ui::widgets
