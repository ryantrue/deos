// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "lvgl.h"

namespace deos::ui::widgets {

lv_obj_t* label(lv_obj_t* parent,
                const char* text,
                lv_color_t text_color,
                const lv_font_t* font = LV_FONT_DEFAULT);

lv_obj_t* tile(lv_obj_t* parent,
               int x,
               int y,
               int width,
               const char* title,
               const char* subtitle,
               lv_color_t background,
               lv_color_t border);

lv_obj_t* settings_row(lv_obj_t* parent,
                       const char* title,
                       const char* subtitle,
                       bool enabled = true);

lv_obj_t* action(lv_obj_t* parent,
                 const char* text,
                 lv_color_t background,
                 lv_color_t foreground,
                 int width = 296);

}  // namespace deos::ui::widgets
