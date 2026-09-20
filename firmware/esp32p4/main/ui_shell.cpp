// SPDX-License-Identifier: Apache-2.0

#include "ui_shell.hpp"

#include "lvgl.h"

namespace deos::ui {
namespace {

constexpr int kScreenSize = 720;

lv_obj_t* make_label(lv_obj_t* parent,
                     const char* text,
                     int x,
                     int y,
                     lv_color_t color,
                     const lv_font_t* font = LV_FONT_DEFAULT) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, font, 0);
    return label;
}

lv_obj_t* make_card(lv_obj_t* parent,
                    int x,
                    int y,
                    int width,
                    int height) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x131A22), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x273241), 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    return card;
}

}  // namespace

void create_boot_shell(lv_display_t* display) {
    lv_display_set_default(display);

    lv_obj_t* screen = lv_screen_active();
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(screen, kScreenSize, kScreenSize);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x090D12), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    lv_obj_t* header = lv_obj_create(screen);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, kScreenSize, 92);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x0E141B), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);

    make_label(header, "DEOS", 24, 16, lv_color_hex(0xF2F6FA), &lv_font_montserrat_28);
    make_label(header, "Declarative Edge OS", 26, 54, lv_color_hex(0x7F8D9C));

    lv_obj_t* badge = lv_obj_create(header);
    lv_obj_set_pos(badge, 540, 23);
    lv_obj_set_size(badge, 150, 46);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0x113224), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(0x2B9E68), 0);
    lv_obj_set_style_radius(badge, 23, 0);
    make_label(badge, "P4  ONLINE", 22, 12, lv_color_hex(0x66E0A3));

    lv_obj_t* system = make_card(screen, 24, 116, 672, 156);
    make_label(system, "SYSTEM", 0, 0, lv_color_hex(0x718092));
    make_label(system, "System/device", 0, 32, lv_color_hex(0xF2F6FA), &lv_font_montserrat_20);
    make_label(system, "Desired state reconciled", 0, 72, lv_color_hex(0xAAB5C1));
    make_label(system, "READY", 544, 40, lv_color_hex(0x66E0A3), &lv_font_montserrat_20);

    lv_obj_t* display_card = make_card(screen, 24, 292, 672, 184);
    make_label(display_card, "DISPLAY", 0, 0, lv_color_hex(0x718092));
    make_label(display_card, "Display/primary", 0, 32, lv_color_hex(0xF2F6FA), &lv_font_montserrat_20);
    make_label(display_card, "ST7703  ·  MIPI-DSI  ·  RGB565", 0, 78, lv_color_hex(0xAAB5C1));
    make_label(display_card, "720 × 720", 0, 112, lv_color_hex(0x5DB0FF), &lv_font_montserrat_20);

    lv_obj_t* architecture = make_card(screen, 24, 496, 672, 128);
    make_label(architecture, "CONTROL PLANE", 0, 0, lv_color_hex(0x718092));
    make_label(architecture, "Resource  →  Plan  →  Reconcile  →  Status", 0, 38,
               lv_color_hex(0xD8E0E8));
    make_label(architecture, "UI is now driven by a DEOS resource", 0, 72,
               lv_color_hex(0x7F8D9C));

    make_label(screen, "v0.1 bring-up  ·  ESP-IDF 6.1  ·  LVGL 9.5", 24, 666,
               lv_color_hex(0x566372));
}

}  // namespace deos::ui
