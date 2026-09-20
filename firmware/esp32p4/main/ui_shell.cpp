// SPDX-License-Identifier: Apache-2.0

#include "ui_shell.hpp"

namespace deos::ui {
namespace {

constexpr int kScreen = 720;
constexpr int kMargin = 24;
constexpr int kGap = 12;
constexpr int kColumn = 216;
constexpr int kTileHeight = 150;
constexpr int kWide = kColumn * 2 + kGap;

constexpr lv_color_t color(uint32_t rgb) {
    return lv_color_hex(rgb);
}

lv_obj_t* label(lv_obj_t* parent,
                const char* text,
                int x,
                int y,
                lv_color_t text_color,
                const lv_font_t* font = LV_FONT_DEFAULT) {
    lv_obj_t* obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_color(obj, text_color, 0);
    lv_obj_set_style_text_font(obj, font, 0);
    return obj;
}

lv_obj_t* tile(lv_obj_t* parent,
               int x,
               int y,
               int width,
               lv_color_t bg,
               lv_color_t border = color(0x2A3039)) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, kTileHeight);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(obj, bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, border, 0);
    lv_obj_set_style_radius(obj, 26, 0);
    lv_obj_set_style_pad_all(obj, 18, 0);
    return obj;
}

void tile_heading(lv_obj_t* obj, const char* name, const char* state) {
    label(obj, name, 0, 0, color(0xF5F7FA), &lv_font_montserrat_20);
    label(obj, state, 0, 34, color(0xB3BECA));
}

void status_pill(lv_obj_t* parent,
                 const char* text,
                 int x,
                 lv_color_t bg,
                 lv_color_t fg) {
    lv_obj_t* pill = lv_obj_create(parent);
    lv_obj_set_pos(pill, x, 9);
    lv_obj_set_size(pill, 112, 34);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(pill, bg, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_set_style_radius(pill, 17, 0);
    lv_obj_set_style_pad_all(pill, 0, 0);

    lv_obj_t* text_obj = label(pill, text, 0, 0, fg);
    lv_obj_center(text_obj);
}

}  // namespace

void create_home_shell(lv_display_t* display) {
    lv_display_set_default(display);

    lv_obj_t* screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(screen, kScreen, kScreen);
    lv_obj_set_style_bg_color(screen, color(0x080A0E), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    // Calm system chrome inspired by mobile OSes; the main surface stays tile-centric.
    label(screen, "DEOS", kMargin, 16, color(0xF5F7FA), &lv_font_montserrat_20);
    status_pill(screen, "LOCAL", 584, color(0x14241C), color(0x69D39A));

    label(screen, "Home", kMargin, 55, color(0xF5F7FA), &lv_font_montserrat_28);
    label(screen, "Your device, services and intelligence", kMargin, 88, color(0x75808D));

    // Row 1: optional intelligence + device state.
    lv_obj_t* ai = tile(screen, kMargin, 120, kWide, color(0x14243B), color(0x285887));
    tile_heading(ai, "AI", "Optional model bridge");
    label(ai, "Connect Qwen or any compatible endpoint", 0, 78, color(0xC9D8EA));
    label(ai, "OS works without it", 0, 108, color(0x68A9EA));

    lv_obj_t* system = tile(screen, 480, 120, kColumn, color(0x171B21));
    tile_heading(system, "System", "READY");
    label(system, "P4", 0, 77, color(0xF5F7FA), &lv_font_montserrat_28);
    label(system, "32 MB PSRAM", 0, 112, color(0x7E8996));

    // Row 2: the OS is useful even with no AI provider configured.
    lv_obj_t* control = tile(screen, kMargin, 282, kColumn, color(0x17251F), color(0x28573F));
    tile_heading(control, "Control", "Local-first");
    label(control, "Entities", 0, 78, color(0x9EE2BB));
    label(control, "0 connected", 0, 108, color(0x6C9F80));

    lv_obj_t* automations = tile(screen, 252, 282, kColumn, color(0x261E31), color(0x573A6E));
    tile_heading(automations, "Automations", "On-device");
    label(automations, "Rules", 0, 78, color(0xD8B4EF));
    label(automations, "0 active", 0, 108, color(0x9778AA));

    lv_obj_t* files = tile(screen, 480, 282, kColumn, color(0x202117), color(0x55562A));
    tile_heading(files, "Files", "Storage");
    label(files, "Internal", 0, 78, color(0xE0D98F));
    label(files, "SD optional", 0, 108, color(0x9C9869));

    // Row 3: app surface and system configuration.
    lv_obj_t* apps = tile(screen, kMargin, 444, kColumn, color(0x1D1C1B));
    tile_heading(apps, "Apps", "Built-in");
    label(apps, "6", 0, 73, color(0xF5F7FA), &lv_font_montserrat_28);
    label(apps, "more later", 0, 110, color(0x7E8996));

    lv_obj_t* settings = tile(screen, 252, 444, kWide, color(0x171B21));
    tile_heading(settings, "Settings", "Device control");
    label(settings, "Network  ·  Display  ·  Storage  ·  Developer", 0, 80, color(0xC4CCD6));
    label(settings, "Everything remains locally configurable", 0, 110, color(0x75808D));

    // A restrained mobile-style dock. It becomes interactive when Input/touch lands.
    lv_obj_t* dock = lv_obj_create(screen);
    lv_obj_set_pos(dock, kMargin, 616);
    lv_obj_set_size(dock, 672, 76);
    lv_obj_remove_flag(dock, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(dock, color(0x11151B), 0);
    lv_obj_set_style_bg_opa(dock, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dock, 1, 0);
    lv_obj_set_style_border_color(dock, color(0x252B34), 0);
    lv_obj_set_style_radius(dock, 30, 0);

    label(dock, "HOME", 34, 27, color(0xF5F7FA));
    label(dock, "SEARCH", 185, 27, color(0x7D8895));
    label(dock, "AI", 374, 27, color(0x7D8895));
    label(dock, "APPS", 521, 27, color(0x7D8895));

    lv_obj_t* indicator = lv_obj_create(screen);
    lv_obj_set_pos(indicator, 306, 706);
    lv_obj_set_size(indicator, 108, 5);
    lv_obj_set_style_bg_color(indicator, color(0xD4D8DD), 0);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(indicator, 0, 0);
    lv_obj_set_style_radius(indicator, 3, 0);
}

}  // namespace deos::ui
