// SPDX-License-Identifier: Apache-2.0

#include "ui_shell.hpp"

#include "network_controller.hpp"
#include "storage_controller.hpp"

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

namespace deos::ui {
namespace {

constexpr int kScreen = 720;
constexpr int kMargin = 24;
constexpr int kGap = 12;
constexpr int kHeaderHeight = 104;
constexpr int kTileHeight = 146;
constexpr int kRadius = 24;

lv_color_t color(uint32_t rgb) {
    return lv_color_hex(rgb);
}

lv_obj_t* make_label(lv_obj_t* parent,
                     const char* text,
                     lv_color_t text_color,
                     const lv_font_t* font = LV_FONT_DEFAULT) {
    lv_obj_t* obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_color(obj, text_color, 0);
    lv_obj_set_style_text_font(obj, font, 0);
    return obj;
}

void set_panel_style(lv_obj_t* obj,
                     lv_color_t bg = color(0x15191F),
                     lv_color_t border = color(0x292F38)) {
    lv_obj_set_style_bg_color(obj, bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, border, 0);
    lv_obj_set_style_radius(obj, kRadius, 0);
    lv_obj_set_style_pad_all(obj, 18, 0);
}

std::string bytes_human(uint64_t bytes) {
    char buffer[32]{};
    const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mib >= 1024.0) {
        std::snprintf(buffer, sizeof(buffer), "%.1f GB", mib / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.0f MB", mib);
    }
    return buffer;
}

}  // namespace

struct ShellUi::Impl {
    lv_display_t* display{nullptr};
    platform::NetworkController& network;
    platform::StorageController& storage;
    lv_timer_t* storage_timer{nullptr};
    lv_obj_t* storage_body{nullptr};
    int developer_taps{0};
    bool developer_mode{false};

    Impl(lv_display_t* display_handle,
         platform::NetworkController& network_controller,
         platform::StorageController& storage_controller)
        : display(display_handle),
          network(network_controller),
          storage(storage_controller) {}

    ~Impl() {
        stop_storage_timer();
    }

    void stop_storage_timer() {
        if (storage_timer != nullptr) {
            lv_timer_delete(storage_timer);
            storage_timer = nullptr;
        }
        storage_body = nullptr;
    }

    lv_obj_t* begin_screen(const char* title, bool show_back) {
        stop_storage_timer();
        lv_display_set_default(display);

        lv_obj_t* screen = lv_screen_active();
        lv_obj_clean(screen);
        lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(screen, kScreen, kScreen);
        lv_obj_set_style_bg_color(screen, color(0x080A0E), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(screen, 0, 0);

        if (show_back) {
            lv_obj_t* back = lv_button_create(screen);
            lv_obj_set_pos(back, kMargin, 18);
            lv_obj_set_size(back, 76, 52);
            lv_obj_set_style_radius(back, 20, 0);
            lv_obj_set_style_bg_color(back, color(0x171C23), 0);
            lv_obj_set_style_border_width(back, 1, 0);
            lv_obj_set_style_border_color(back, color(0x2A313A), 0);
            lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, this);

            lv_obj_t* arrow = make_label(back, "<", color(0xF5F7FA), &lv_font_montserrat_28);
            lv_obj_center(arrow);
        } else {
            lv_obj_t* brand = make_label(screen, "DEOS", color(0xF5F7FA), &lv_font_montserrat_20);
            lv_obj_set_pos(brand, kMargin, 27);
        }

        lv_obj_t* heading = make_label(screen, title, color(0xF5F7FA), &lv_font_montserrat_28);
        lv_obj_set_pos(heading, show_back ? 120 : kMargin, 23);

        lv_obj_t* local = lv_obj_create(screen);
        lv_obj_set_pos(local, 582, 18);
        lv_obj_set_size(local, 114, 42);
        lv_obj_remove_flag(local, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(local, color(0x14241C), 0);
        lv_obj_set_style_bg_opa(local, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(local, 0, 0);
        lv_obj_set_style_radius(local, 21, 0);
        lv_obj_set_style_pad_all(local, 0, 0);

        lv_obj_t* local_text = make_label(local, "LOCAL", color(0x69D39A));
        lv_obj_center(local_text);

        return screen;
    }

    lv_obj_t* make_tile(lv_obj_t* parent,
                        int x,
                        int y,
                        int width,
                        const char* title,
                        const char* subtitle,
                        lv_color_t bg,
                        lv_color_t border) {
        lv_obj_t* tile = lv_button_create(parent);
        lv_obj_set_pos(tile, x, y);
        lv_obj_set_size(tile, width, kTileHeight);
        set_panel_style(tile, bg, border);
        lv_obj_set_style_shadow_width(tile, 0, 0);
        lv_obj_set_style_transform_scale(tile, 248, LV_STATE_PRESSED);

        lv_obj_t* title_label = make_label(tile, title, color(0xF5F7FA), &lv_font_montserrat_20);
        lv_obj_set_pos(title_label, 0, 0);

        lv_obj_t* subtitle_label = make_label(tile, subtitle, color(0x8E99A6));
        lv_obj_set_pos(subtitle_label, 0, 36);
        return tile;
    }

    lv_obj_t* make_row(lv_obj_t* parent,
                       const char* title,
                       const char* subtitle,
                       bool enabled = true) {
        lv_obj_t* row = enabled ? lv_button_create(parent) : lv_obj_create(parent);
        lv_obj_set_width(row, 652);
        lv_obj_set_height(row, 86);
        set_panel_style(row, color(0x12161C), color(0x262C34));
        lv_obj_set_style_pad_all(row, 14, 0);
        if (enabled) {
            lv_obj_set_style_shadow_width(row, 0, 0);
            lv_obj_set_style_transform_scale(row, 250, LV_STATE_PRESSED);
        }

        lv_obj_t* title_label = make_label(
            row, title, enabled ? color(0xF5F7FA) : color(0x6E7884), &lv_font_montserrat_20);
        lv_obj_set_pos(title_label, 0, 0);

        lv_obj_t* subtitle_label = make_label(
            row, subtitle, enabled ? color(0x87929F) : color(0x555E69));
        lv_obj_set_pos(subtitle_label, 0, 34);

        if (enabled) {
            lv_obj_t* chevron = make_label(row, ">", color(0x626D79), &lv_font_montserrat_20);
            lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, 0, 0);
        }
        return row;
    }

    lv_obj_t* make_action(lv_obj_t* parent,
                          const char* text,
                          lv_color_t bg,
                          lv_color_t fg,
                          int width = 296) {
        lv_obj_t* button = lv_button_create(parent);
        lv_obj_set_size(button, width, 58);
        lv_obj_set_style_bg_color(button, bg, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_radius(button, 20, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_transform_scale(button, 248, LV_STATE_PRESSED);
        lv_obj_t* label = make_label(button, text, fg, &lv_font_montserrat_20);
        lv_obj_center(label);
        return button;
    }

    void show_home() {
        lv_obj_t* screen = begin_screen("Home", false);

        lv_obj_t* intro = make_label(
            screen,
            "A local-first device surface. Tap a card to open it.",
            color(0x75808D));
        lv_obj_set_pos(intro, kMargin, 72);

        constexpr int col = 216;
        constexpr int wide = 444;

        lv_obj_t* ai = make_tile(
            screen, kMargin, 112, wide, "AI", "Optional intelligence layer",
            color(0x14243B), color(0x285887));
        lv_obj_t* ai_hint = make_label(ai, "Provider not configured", color(0x68A9EA));
        lv_obj_set_pos(ai_hint, 0, 84);

        lv_obj_t* system = make_tile(
            screen, 480, 112, col, "System", "Device health",
            color(0x171B21), color(0x2A3039));
        lv_obj_t* sys_value = make_label(system, "READY", color(0x71D99C), &lv_font_montserrat_20);
        lv_obj_set_pos(sys_value, 0, 84);
        lv_obj_add_event_cb(system, on_system, LV_EVENT_CLICKED, this);

        lv_obj_t* control = make_tile(
            screen, kMargin, 270, col, "Control", "State + Actions",
            color(0x17251F), color(0x28573F));
        lv_obj_t* c = make_label(control, "0 entities", color(0x9EE2BB));
        lv_obj_set_pos(c, 0, 84);

        lv_obj_t* automations = make_tile(
            screen, 252, 270, col, "Automations", "On-device rules",
            color(0x261E31), color(0x573A6E));
        lv_obj_t* a = make_label(automations, "0 active", color(0xD8B4EF));
        lv_obj_set_pos(a, 0, 84);

        const auto sd = storage.snapshot();
        lv_obj_t* files = make_tile(
            screen, 480, 270, col, "Storage", "Internal + SD",
            color(0x202117), color(0x55562A));
        lv_obj_t* sd_state = make_label(
            files,
            platform::to_string(sd.state),
            sd.state == platform::SdVolumeState::Ready
                ? color(0xD8D68A)
                : color(0xA8A570));
        lv_obj_set_pos(sd_state, 0, 84);
        lv_obj_add_event_cb(files, on_storage, LV_EVENT_CLICKED, this);

        lv_obj_t* apps = make_tile(
            screen, kMargin, 428, col, "Apps", "Built-in surface",
            color(0x1D1C1B), color(0x343330));
        lv_obj_t* app_count = make_label(apps, "6 system apps", color(0xA9B1BA));
        lv_obj_set_pos(app_count, 0, 84);

        lv_obj_t* settings = make_tile(
            screen, 252, 428, wide, "Settings", "Network, display, storage, developer",
            color(0x171B21), color(0x2A3039));
        lv_obj_t* settings_hint = make_label(
            settings, "Everything locally configurable", color(0x8C97A4));
        lv_obj_set_pos(settings_hint, 0, 84);
        lv_obj_add_event_cb(settings, on_settings, LV_EVENT_CLICKED, this);

        lv_obj_t* dock = lv_obj_create(screen);
        lv_obj_set_pos(dock, kMargin, 610);
        lv_obj_set_size(dock, 672, 82);
        lv_obj_remove_flag(dock, LV_OBJ_FLAG_SCROLLABLE);
        set_panel_style(dock, color(0x10141A), color(0x242A32));
        lv_obj_set_style_pad_all(dock, 10, 0);

        const char* dock_labels[] = {"HOME", "SEARCH", "AI", "APPS"};
        for (int i = 0; i < 4; ++i) {
            lv_obj_t* item = lv_button_create(dock);
            lv_obj_set_pos(item, i * 160, 0);
            lv_obj_set_size(item, 150, 60);
            lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(item, 0, 0);
            lv_obj_set_style_shadow_width(item, 0, 0);
            lv_obj_t* text = make_label(
                item,
                dock_labels[i],
                i == 0 ? color(0xF5F7FA) : color(0x697481));
            lv_obj_center(text);
            if (i == 0) {
                lv_obj_add_event_cb(item, on_home, LV_EVENT_CLICKED, this);
            }
        }

        (void)ai;
        (void)control;
        (void)automations;
        (void)apps;
    }

    void show_settings() {
        lv_obj_t* screen = begin_screen("Settings", true);

        lv_obj_t* body = lv_obj_create(screen);
        lv_obj_set_pos(body, 24, kHeaderHeight);
        lv_obj_set_size(body, 672, 574);
        lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(body, 0, 0);
        lv_obj_set_style_pad_all(body, 10, 0);
        lv_obj_set_style_pad_row(body, 10, 0);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);

        lv_obj_t* network_row = make_row(body, "Network", "Wi-Fi and local control plane");
        lv_obj_add_event_cb(network_row, on_network, LV_EVENT_CLICKED, this);

        make_row(body, "Display", "Brightness and sleep", false);

        lv_obj_t* storage_row = make_row(body, "Storage", "SD card and DEOS volume");
        lv_obj_add_event_cb(storage_row, on_storage, LV_EVENT_CLICKED, this);

        if (developer_mode) {
            lv_obj_t* developer = make_row(body, "Developer", "Diagnostics and debug tools");
            lv_obj_add_event_cb(developer, on_developer, LV_EVENT_CLICKED, this);
        }

        lv_obj_t* about = make_row(body, "About DEOS", "System, build and hardware");
        lv_obj_add_event_cb(about, on_system, LV_EVENT_CLICKED, this);

        lv_obj_t* note = make_label(
            body,
            developer_mode
                ? "Developer mode is enabled for this boot."
                : "Tip: developer controls stay hidden during normal use.",
            color(0x606B77));
        lv_obj_set_style_pad_top(note, 8, 0);
    }

    void add_info_row(lv_obj_t* parent, const char* key, const std::string& value) {
        lv_obj_t* row = lv_obj_create(parent);
        lv_obj_set_size(row, 620, 54);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);

        lv_obj_t* left = make_label(row, key, color(0x7F8A97));
        lv_obj_align(left, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* right = make_label(row, value.c_str(), color(0xE6EAF0));
        lv_obj_align(right, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    void show_network() {
        lv_obj_t* screen = begin_screen("Network", true);
        const platform::NetworkSnapshot net = network.snapshot();

        lv_obj_t* card = lv_obj_create(screen);
        lv_obj_set_pos(card, 24, 118);
        lv_obj_set_size(card, 672, 430);
        set_panel_style(card, color(0x11151A), color(0x262C34));
        lv_obj_set_style_pad_all(card, 26, 0);

        const char* state_text = net.provisioning
                                     ? "SETUP"
                                     : (net.connected ? "CONNECTED" : "CONNECTING");
        lv_obj_t* state = make_label(
            card,
            state_text,
            net.connected ? color(0x72D69D) : color(0xE2C66F),
            &lv_font_montserrat_28);
        lv_obj_set_pos(state, 0, 0);

        if (!net.initialized) {
            lv_obj_t* note = make_label(
                card,
                "Network service is still starting.",
                color(0x8C97A4));
            lv_obj_set_pos(note, 0, 56);
            return;
        }

        if (net.provisioning) {
            lv_obj_t* title = make_label(
                card,
                "Connect a phone or computer to this setup network:",
                color(0x9AA5B2));
            lv_obj_set_pos(title, 0, 62);

            lv_obj_t* ssid = make_label(
                card, net.setup_ssid.c_str(), color(0xF4F7FA), &lv_font_montserrat_28);
            lv_obj_set_pos(ssid, 0, 104);

            const std::string password = "Password: " + net.setup_password;
            lv_obj_t* pass = make_label(card, password.c_str(), color(0xC8D1DB));
            lv_obj_set_pos(pass, 0, 150);

            lv_obj_t* step = make_label(
                card,
                "Then open http://192.168.4.1/ and choose the Wi-Fi network\n"
                "DEOS should use. Credentials are stored only on this device.",
                color(0x7E8996));
            lv_label_set_long_mode(step, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(step, 610);
            lv_obj_set_pos(step, 0, 205);
        } else {
            lv_obj_t* details = lv_obj_create(card);
            lv_obj_set_pos(details, 0, 58);
            lv_obj_set_size(details, 620, 250);
            lv_obj_set_style_bg_opa(details, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(details, 0, 0);
            lv_obj_set_style_pad_all(details, 0, 0);
            lv_obj_set_style_pad_row(details, 2, 0);
            lv_obj_set_flex_flow(details, LV_FLEX_FLOW_COLUMN);

            add_info_row(details, "Wi-Fi", net.ssid.empty() ? "configured" : net.ssid);
            add_info_row(details, "IP", net.ip.empty() ? "waiting for DHCP" : net.ip);
            add_info_row(details, "Local name", "deos.local");
            add_info_row(details, "Control API", "port 80 / token auth");

            lv_obj_t* forget = make_action(
                screen, "Forget Wi-Fi...", color(0x3A2023), color(0xF2B2B7), 672);
            lv_obj_set_pos(forget, 24, 576);
            lv_obj_add_event_cb(forget, on_forget_confirm, LV_EVENT_CLICKED, this);
        }
    }

    void show_forget_wifi_confirm() {
        lv_obj_t* screen = begin_screen("Forget Wi-Fi?", true);

        lv_obj_t* card = lv_obj_create(screen);
        lv_obj_set_pos(card, 24, 138);
        lv_obj_set_size(card, 672, 300);
        set_panel_style(card, color(0x24171A), color(0x633239));
        lv_obj_set_style_pad_all(card, 28, 0);

        lv_obj_t* title = make_label(
            card,
            "DEOS will remove the saved Wi-Fi profile.",
            color(0xF4B4BA),
            &lv_font_montserrat_28);
        lv_obj_set_pos(title, 0, 0);

        lv_obj_t* detail = make_label(
            card,
            "The device API token is kept. After reboot DEOS will return to\n"
            "its setup access point so Wi-Fi can be configured again.",
            color(0xC6B9BC));
        lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(detail, 610);
        lv_obj_set_pos(detail, 0, 78);

        lv_obj_t* cancel = make_action(
            screen, "Cancel", color(0x20252D), color(0xE2E7ED), 316);
        lv_obj_set_pos(cancel, 24, 492);
        lv_obj_add_event_cb(cancel, on_network, LV_EVENT_CLICKED, this);

        lv_obj_t* forget = make_action(
            screen, "Forget and reboot", color(0x8D3039), color(0xFFFFFF), 340);
        lv_obj_set_pos(forget, 356, 492);
        lv_obj_add_event_cb(forget, on_forget_wifi, LV_EVENT_CLICKED, this);
    }

    void show_developer() {
        lv_obj_t* screen = begin_screen("Developer", true);

        lv_obj_t* card = lv_obj_create(screen);
        lv_obj_set_pos(card, 24, 110);
        lv_obj_set_size(card, 672, 510);
        set_panel_style(card, color(0x10161B), color(0x28404A));
        lv_obj_set_style_pad_all(card, 24, 0);
        lv_obj_set_style_pad_row(card, 2, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

        const platform::NetworkSnapshot net = network.snapshot();
        const platform::SdVolumeSnapshot sd = storage.snapshot();
        const esp_partition_t* running = esp_ota_get_running_partition();

        const uint64_t uptime_sec =
            static_cast<uint64_t>(esp_timer_get_time()) / 1000000ULL;
        add_info_row(card, "Uptime", std::to_string(uptime_sec) + " s");
        add_info_row(card, "Tasks", std::to_string(uxTaskGetNumberOfTasks()));
        add_info_row(card, "Internal free",
                     bytes_human(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        add_info_row(card, "Largest internal",
                     bytes_human(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        add_info_row(card, "PSRAM free",
                     bytes_human(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        add_info_row(card, "Network",
                     net.provisioning
                         ? "setup-ap"
                         : (net.connected ? "connected" : "offline/connecting"));
        add_info_row(card, "IP", net.ip.empty() ? "-" : net.ip);
        add_info_row(card, "SD", platform::to_string(sd.state));
        add_info_row(card, "OTA slot",
                     running != nullptr ? running->label : "unknown");

        lv_obj_t* note = make_label(
            screen,
            "Developer Mode is intentionally separate from normal device UX.",
            color(0x6E7E89));
        lv_obj_set_pos(note, 28, 646);
    }

    void show_system() {
        lv_obj_t* screen = begin_screen("System", true);

        lv_obj_t* card = lv_obj_create(screen);
        lv_obj_set_pos(card, 24, 110);
        lv_obj_set_size(card, 672, 480);
        set_panel_style(card, color(0x11151A), color(0x262C34));
        lv_obj_set_style_pad_all(card, 24, 0);
        lv_obj_set_style_pad_row(card, 2, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

        const esp_app_desc_t* app = esp_app_get_description();
        const esp_partition_t* running = esp_ota_get_running_partition();

        add_info_row(card, "Board", "Waveshare ESP32-P4 4B");
        add_info_row(card, "CPU", "ESP32-P4 @ 360 MHz");
        add_info_row(card, "PSRAM free",
                     bytes_human(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        add_info_row(card, "Internal RAM free",
                     bytes_human(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        add_info_row(card, "Flash", "32 MB");
        add_info_row(card, "ESP-IDF", app != nullptr ? app->idf_ver : "unknown");
        add_info_row(card, "Build", app != nullptr ? app->version : "unknown");
        add_info_row(card, "OTA slot",
                     running != nullptr ? running->label : "unknown");

        lv_obj_t* build_button = lv_button_create(screen);
        lv_obj_set_pos(build_button, 24, 606);
        lv_obj_set_size(build_button, 672, 76);
        set_panel_style(
            build_button,
            developer_mode ? color(0x14241C) : color(0x13171D),
            developer_mode ? color(0x28573F) : color(0x292F38));
        lv_obj_set_style_shadow_width(build_button, 0, 0);
        lv_obj_add_event_cb(build_button, on_build_tap, LV_EVENT_CLICKED, this);

        lv_obj_t* build_text = make_label(
            build_button,
            developer_mode
                ? "Developer mode enabled"
                : "Build information",
            developer_mode ? color(0x79D7A3) : color(0xD8DEE6),
            &lv_font_montserrat_20);
        lv_obj_align(build_text, LV_ALIGN_LEFT_MID, 0, -10);

        lv_obj_t* hint = make_label(
            build_button,
            developer_mode
                ? "Diagnostics will appear in Settings."
                : "Tap for build details.",
            color(0x6F7A86));
        lv_obj_align(hint, LV_ALIGN_LEFT_MID, 0, 20);
    }

    void render_storage_body() {
        if (storage_body == nullptr) {
            return;
        }

        lv_obj_clean(storage_body);
        const platform::SdVolumeSnapshot sd = storage.snapshot();

        lv_obj_t* status = make_label(
            storage_body,
            platform::to_string(sd.state),
            sd.state == platform::SdVolumeState::Ready
                ? color(0x74D89F)
                : (sd.state == platform::SdVolumeState::Busy
                       ? color(0x7FB4FF)
                       : color(0xE1C777)),
            &lv_font_montserrat_28);
        lv_obj_set_pos(status, 0, 0);

        lv_obj_t* message = make_label(storage_body, sd.message.c_str(), color(0x9AA5B2));
        lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(message, 620);
        lv_obj_set_pos(message, 0, 48);

        int y = 108;
        if (!sd.card_name.empty()) {
            lv_obj_t* card_name = make_label(storage_body, sd.card_name.c_str(), color(0xD7DDE5));
            lv_obj_set_pos(card_name, 0, y);
            y += 34;
        }

        if (sd.total_bytes > 0) {
            const uint64_t used = sd.total_bytes > sd.free_bytes
                                      ? sd.total_bytes - sd.free_bytes
                                      : 0;
            const std::string usage =
                bytes_human(used) + " used  /  " + bytes_human(sd.total_bytes);
            lv_obj_t* usage_label = make_label(storage_body, usage.c_str(), color(0x737F8C));
            lv_obj_set_pos(usage_label, 0, y);
            y += 48;
        }

        if (sd.state == platform::SdVolumeState::Foreign) {
            lv_obj_t* preserve = make_label(
                storage_body,
                "Existing files are untouched until you choose an action.",
                color(0xD9C87F));
            lv_obj_set_pos(preserve, 0, y);
            y += 58;

            lv_obj_t* keep = make_action(
                storage_body, "Leave unchanged", color(0x20252D), color(0xE2E7ED));
            lv_obj_set_pos(keep, 0, y);
            lv_obj_add_event_cb(keep, on_home, LV_EVENT_CLICKED, this);

            lv_obj_t* init = make_action(
                storage_body, "Initialize for DEOS", color(0x245BA5), color(0xFFFFFF));
            lv_obj_set_pos(init, 316, y);
            lv_obj_add_event_cb(init, on_initialize_sd, LV_EVENT_CLICKED, this);

            y += 76;
            lv_obj_t* format = make_action(
                storage_body, "Format instead...", color(0x3A2023), color(0xF1A5AB), 612);
            lv_obj_set_pos(format, 0, y);
            lv_obj_add_event_cb(format, on_format_confirm, LV_EVENT_CLICKED, this);
        } else if (sd.state == platform::SdVolumeState::NeedsFormat) {
            lv_obj_t* warning = make_label(
                storage_body,
                "DEOS will never format this card automatically.",
                color(0xF1A5AB));
            lv_obj_set_pos(warning, 0, y);
            y += 58;

            lv_obj_t* format = make_action(
                storage_body, "Format for DEOS...", color(0x7A2830), color(0xFFFFFF), 612);
            lv_obj_set_pos(format, 0, y);
            lv_obj_add_event_cb(format, on_format_confirm, LV_EVENT_CLICKED, this);
        } else if (sd.state == platform::SdVolumeState::Error) {
            lv_obj_t* warning = make_label(
                storage_body,
                "The card was not classified safely. DEOS will not offer formatting.",
                color(0xF1A5AB));
            lv_label_set_long_mode(warning, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(warning, 610);
            lv_obj_set_pos(warning, 0, y);
            y += 72;

            lv_obj_t* retry = make_action(
                storage_body, "Rescan", color(0x245BA5), color(0xFFFFFF), 612);
            lv_obj_set_pos(retry, 0, y);
            lv_obj_add_event_cb(retry, on_rescan_sd, LV_EVENT_CLICKED, this);
        } else if (sd.state == platform::SdVolumeState::Ready) {
            lv_obj_t* ready = make_label(
                storage_body,
                "DEOS/Apps  ·  AppData  ·  Packages  ·  Backups  ·  Logs\n"
                "Media  ·  Documents  ·  Downloads",
                color(0x8F9BA8));
            lv_label_set_long_mode(ready, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(ready, 610);
            lv_obj_set_pos(ready, 0, y);
        } else if (sd.state == platform::SdVolumeState::Absent) {
            lv_obj_t* hint = make_label(
                storage_body,
                "Insert a microSD card, then ask DEOS to scan the slot again.",
                color(0x737F8C));
            lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(hint, 610);
            lv_obj_set_pos(hint, 0, y);
            y += 72;

            lv_obj_t* retry = make_action(
                storage_body, "Rescan SD card", color(0x245BA5), color(0xFFFFFF), 612);
            lv_obj_set_pos(retry, 0, y);
            lv_obj_add_event_cb(retry, on_rescan_sd, LV_EVENT_CLICKED, this);
        } else if (sd.state == platform::SdVolumeState::Busy) {
            lv_obj_t* progress = lv_spinner_create(storage_body);
            lv_obj_set_size(progress, 58, 58);
            lv_obj_set_pos(progress, 0, y);
            lv_obj_t* op = make_label(
                storage_body,
                sd.operation == "format"
                    ? "Formatting can take a moment. Do not remove the card."
                    : "Working on the SD card...",
                color(0x7F8A97));
            lv_obj_set_pos(op, 80, y + 20);
        }
    }

    void show_storage() {
        lv_obj_t* screen = begin_screen("Storage", true);

        storage_body = lv_obj_create(screen);
        lv_obj_set_pos(storage_body, 24, 112);
        lv_obj_set_size(storage_body, 672, 570);
        set_panel_style(storage_body, color(0x11151A), color(0x262C34));
        lv_obj_set_style_pad_all(storage_body, 24, 0);

        render_storage_body();
        storage_timer = lv_timer_create(on_storage_timer, 500, this);
    }

    void show_format_confirm() {
        lv_obj_t* screen = begin_screen("Erase SD card?", true);

        lv_obj_t* warning = lv_obj_create(screen);
        lv_obj_set_pos(warning, 24, 128);
        lv_obj_set_size(warning, 672, 360);
        set_panel_style(warning, color(0x261417), color(0x713038));
        lv_obj_set_style_pad_all(warning, 28, 0);

        lv_obj_t* title = make_label(
            warning, "This deletes everything on the SD card.", color(0xFFB2B8),
            &lv_font_montserrat_28);
        lv_obj_set_pos(title, 0, 0);

        lv_obj_t* detail = make_label(
            warning,
            "DEOS will replace the card layout with one partition using the\n"
            "whole card, create a FAT volume, then initialize:\n\n"
            "DEOS/Apps, AppData, Packages, Backups, Logs\n"
            "Media/Music, Pictures, Video\n"
            "Documents and Downloads\n\n"
            "This action cannot preserve existing files.",
            color(0xD6C1C3));
        lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(detail, 610);
        lv_obj_set_pos(detail, 0, 64);

        lv_obj_t* cancel = make_action(
            screen, "Cancel", color(0x20252D), color(0xE2E7ED), 316);
        lv_obj_set_pos(cancel, 24, 530);
        lv_obj_add_event_cb(cancel, on_storage, LV_EVENT_CLICKED, this);

        lv_obj_t* erase = make_action(
            screen, "Erase and format", color(0x9A2F39), color(0xFFFFFF), 340);
        lv_obj_set_pos(erase, 356, 530);
        lv_obj_add_event_cb(erase, on_format_sd, LV_EVENT_CLICKED, this);

        lv_obj_t* footnote = make_label(
            screen,
            "Formatting only starts after pressing the red button above.",
            color(0x6F7A86));
        lv_obj_set_pos(footnote, 24, 612);
    }

    static Impl* self(lv_event_t* event) {
        return static_cast<Impl*>(lv_event_get_user_data(event));
    }

    static void on_home(lv_event_t* event) {
        self(event)->show_home();
    }

    static void on_back(lv_event_t* event) {
        self(event)->show_home();
    }

    static void on_settings(lv_event_t* event) {
        self(event)->show_settings();
    }

    static void on_system(lv_event_t* event) {
        self(event)->show_system();
    }

    static void on_developer(lv_event_t* event) {
        self(event)->show_developer();
    }

    static void on_network(lv_event_t* event) {
        self(event)->show_network();
    }

    static void on_forget_confirm(lv_event_t* event) {
        self(event)->show_forget_wifi_confirm();
    }

    static void on_forget_wifi(lv_event_t* event) {
        Impl* ui = self(event);
        if (ui->network.forget_wifi_and_reboot()) {
            lv_obj_t* screen = ui->begin_screen("Rebooting", false);
            lv_obj_t* message = make_label(
                screen,
                "Wi-Fi profile removed. DEOS is restarting into setup mode...",
                color(0xD7DEE7),
                &lv_font_montserrat_20);
            lv_obj_set_pos(message, 48, 180);
        } else {
            ui->show_network();
        }
    }

    static void on_storage(lv_event_t* event) {
        self(event)->show_storage();
    }

    static void on_rescan_sd(lv_event_t* event) {
        Impl* ui = self(event);
        (void)ui->storage.request_rescan();
        ui->show_storage();
    }

    static void on_initialize_sd(lv_event_t* event) {
        Impl* ui = self(event);
        (void)ui->storage.request_initialize_for_deos();
        ui->show_storage();
    }

    static void on_format_confirm(lv_event_t* event) {
        self(event)->show_format_confirm();
    }

    static void on_format_sd(lv_event_t* event) {
        Impl* ui = self(event);
        (void)ui->storage.request_format_for_deos();
        ui->show_storage();
    }

    static void on_build_tap(lv_event_t* event) {
        Impl* ui = self(event);
        if (!ui->developer_mode) {
            ++ui->developer_taps;
            if (ui->developer_taps >= 7) {
                ui->developer_mode = true;
            }
        }
        ui->show_system();
    }

    static void on_storage_timer(lv_timer_t* timer) {
        auto* ui = static_cast<Impl*>(lv_timer_get_user_data(timer));
        if (ui != nullptr) {
            ui->render_storage_body();
        }
    }
};

ShellUi::ShellUi(lv_display_t* display,
                 platform::NetworkController& network,
                 platform::StorageController& storage)
    : impl_(std::make_unique<Impl>(display, network, storage)) {}

ShellUi::~ShellUi() = default;

void ShellUi::create() {
    impl_->show_home();
}

}  // namespace deos::ui
