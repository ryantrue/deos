// SPDX-License-Identifier: Apache-2.0

#include "display_controller.hpp"
#include "device_preferences.hpp"
#include "ui_runtime.hpp"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7703.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>

namespace deos::platform {
namespace {

constexpr char kTag[] = "deos-display";
constexpr int kWidth = 720;
constexpr int kHeight = 720;
constexpr gpio_num_t kResetGpio = GPIO_NUM_27;
constexpr gpio_num_t kBacklightGpio = GPIO_NUM_26;
constexpr int kDsiLanes = 2;
constexpr int kDsiLaneMbps = 480;
constexpr int kDpiClockMhz = 38;
constexpr int kMipiLdoChannel = 3;
constexpr int kMipiLdoMv = 2500;
constexpr int kBoardLdoChannel = 4;
constexpr int kBoardLdoMv = 3300;
constexpr int kDrawLines = 40;
constexpr int kLvglTickMs = 2;
constexpr uint32_t kLvglTaskStack = 6144;
constexpr UBaseType_t kLvglTaskPriority = 4;
constexpr ledc_mode_t kLedcMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kLedcTimer = LEDC_TIMER_1;
constexpr ledc_channel_t kLedcChannel = LEDC_CHANNEL_1;
constexpr uint32_t kLedcMaxDuty = 1023;

int parse_brightness(const Resource& desired) {
    const auto it = desired.spec.find("brightness");
    if (it == desired.spec.end()) {
        return 70;
    }

    char* end = nullptr;
    const long parsed = std::strtol(it->second.c_str(), &end, 10);
    if (end == it->second.c_str() || *end != '\0') {
        return 70;
    }
    return static_cast<int>(std::clamp<long>(parsed, 0, 100));
}

void lvgl_tick(void*) {
    lv_tick_inc(kLvglTickMs);
}

void lvgl_task(void*) {
    ESP_LOGI(kTag, "LVGL task started");
    auto& runtime = ui::UiRuntime::instance();
    while (true) {
        uint32_t delay_ms = 10;
        {
            ui::UiLock lock(runtime);
            if (lock.locked()) {
                delay_ms = lv_timer_handler();
            }
        }
        if (delay_ms < 1) {
            delay_ms = 1;
        } else if (delay_ms > 20) {
            delay_ms = 20;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void lvgl_flush(lv_display_t* display, const lv_area_t* area, uint8_t* px_map) {
    auto panel = static_cast<esp_lcd_panel_handle_t>(lv_display_get_user_data(display));
    const esp_err_t err = esp_lcd_panel_draw_bitmap(panel,
                                                     area->x1,
                                                     area->y1,
                                                     area->x2 + 1,
                                                     area->y2 + 1,
                                                     px_map);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "draw bitmap failed: %s", esp_err_to_name(err));
        lv_display_flush_ready(display);
    }
}

bool notify_flush_ready(esp_lcd_panel_handle_t,
                        esp_lcd_dpi_panel_event_data_t*,
                        void* user_ctx) {
    auto* display = static_cast<lv_display_t*>(user_ctx);
    lv_display_flush_ready(display);
    return false;
}

}  // namespace

struct DisplayController::Impl {
    esp_ldo_channel_handle_t mipi_ldo{nullptr};
    esp_ldo_channel_handle_t board_ldo{nullptr};
    esp_lcd_dsi_bus_handle_t dsi_bus{nullptr};
    esp_lcd_panel_io_handle_t io{nullptr};
    esp_lcd_panel_handle_t panel{nullptr};
    esp_timer_handle_t tick_timer{nullptr};
    lv_display_t* display{nullptr};
    TaskHandle_t lvgl_task_handle{nullptr};
    void* draw_buffer_a{nullptr};
    void* draw_buffer_b{nullptr};
    bool initialized{false};
    int brightness{0};

    esp_err_t init_backlight() {
        ledc_timer_config_t timer{};
        timer.speed_mode = kLedcMode;
        timer.duty_resolution = LEDC_TIMER_10_BIT;
        timer.timer_num = kLedcTimer;
        timer.freq_hz = 5000;
        timer.clk_cfg = LEDC_AUTO_CLK;
        ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), kTag, "LEDC timer init failed");

        ledc_channel_config_t channel{};
        channel.gpio_num = kBacklightGpio;
        channel.speed_mode = kLedcMode;
        channel.channel = kLedcChannel;
        channel.timer_sel = kLedcTimer;
        channel.duty = 0;
        channel.hpoint = 0;
        channel.flags.output_invert = 1;
        ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), kTag, "LEDC channel init failed");
        return ESP_OK;
    }

    esp_err_t set_brightness(int percent) {
        percent = std::clamp(percent, 0, 100);
        const uint32_t duty = (kLedcMaxDuty * static_cast<uint32_t>(percent)) / 100U;
        ESP_RETURN_ON_ERROR(ledc_set_duty(kLedcMode, kLedcChannel, duty), kTag,
                            "set backlight duty failed");
        ESP_RETURN_ON_ERROR(ledc_update_duty(kLedcMode, kLedcChannel), kTag,
                            "update backlight duty failed");
        brightness = percent;
        ESP_LOGI(kTag, "Backlight: %d%%", percent);
        return ESP_OK;
    }

    esp_err_t acquire_power() {
        esp_ldo_channel_config_t mipi{};
        mipi.chan_id = kMipiLdoChannel;
        mipi.voltage_mv = kMipiLdoMv;
        ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&mipi, &mipi_ldo), kTag,
                            "MIPI D-PHY LDO failed");

        esp_ldo_channel_config_t board{};
        board.chan_id = kBoardLdoChannel;
        board.voltage_mv = kBoardLdoMv;
        ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&board, &board_ldo), kTag,
                            "board LDO VO4 failed");
        return ESP_OK;
    }

    esp_err_t init_panel() {
        esp_lcd_dsi_bus_config_t bus{};
        bus.bus_id = 0;
        bus.num_data_lanes = kDsiLanes;
        bus.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
        bus.lane_bit_rate_mbps = kDsiLaneMbps;
        ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus, &dsi_bus), kTag,
                            "create DSI bus failed");

        esp_lcd_dbi_io_config_t dbi{};
        dbi.virtual_channel = 0;
        dbi.lcd_cmd_bits = 8;
        dbi.lcd_param_bits = 8;
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi, &io), kTag,
                            "create DBI IO failed");

        esp_lcd_dpi_panel_config_t dpi{};
        dpi.virtual_channel = 0;
        dpi.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
        dpi.dpi_clock_freq_mhz = kDpiClockMhz;
        dpi.in_color_format = LCD_COLOR_FMT_RGB565;
        // Keep two complete panel buffers. LVGL DIRECT mode below renders only
        // invalidated regions into these screen-sized buffers; FULL mode forces
        // a complete 720x720 software redraw on every refresh and can starve
        // IDLE0 long enough to trip the task watchdog.
        dpi.num_fbs = 2;
        dpi.video_timing.h_size = kWidth;
        dpi.video_timing.v_size = kHeight;
        dpi.video_timing.hsync_back_porch = 50;
        dpi.video_timing.hsync_pulse_width = 20;
        dpi.video_timing.hsync_front_porch = 50;
        dpi.video_timing.vsync_back_porch = 20;
        dpi.video_timing.vsync_pulse_width = 4;
        dpi.video_timing.vsync_front_porch = 20;

        st7703_vendor_config_t vendor{};
        vendor.flags.use_mipi_interface = 1;
        vendor.mipi_config.dsi_bus = dsi_bus;
        vendor.mipi_config.dpi_config = &dpi;

        esp_lcd_panel_dev_config_t dev{};
        dev.reset_gpio_num = kResetGpio;
        dev.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        dev.data_endian = LCD_RGB_DATA_ENDIAN_BIG;
        dev.bits_per_pixel = 16;
        dev.vendor_config = &vendor;

        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7703(io, &dev, &panel), kTag,
                            "create ST7703 panel failed");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), kTag, "panel reset failed");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), kTag, "panel init failed");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), kTag,
                            "panel display-on failed");
        return ESP_OK;
    }

    esp_err_t init_lvgl() {
        lv_init();

        display = lv_display_create(kWidth, kHeight);
        if (display == nullptr) {
            return ESP_ERR_NO_MEM;
        }
        lv_display_set_user_data(display, panel);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);

        // Keep LVGL's software render buffers independent from the DPI panel's
        // scan-out framebuffers. DIRECT/FULL rendering against the panel buffers
        // can monopolize a P4 core during 720x720 redraws and stalls input.
        constexpr size_t kBytesPerPixel = 2;
        const size_t draw_bytes =
            static_cast<size_t>(kWidth) * kDrawLines * kBytesPerPixel;
        draw_buffer_a = heap_caps_aligned_calloc(
            64, 1, draw_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        draw_buffer_b = heap_caps_aligned_calloc(
            64, 1, draw_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (draw_buffer_a == nullptr || draw_buffer_b == nullptr) {
            ESP_LOGE(kTag, "LVGL draw-buffer allocation failed (%u bytes each)",
                     static_cast<unsigned>(draw_bytes));
            return ESP_ERR_NO_MEM;
        }

        lv_display_set_buffers(display,
                               draw_buffer_a,
                               draw_buffer_b,
                               draw_bytes,
                               LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(display, lvgl_flush);

        esp_lcd_dpi_panel_event_callbacks_t callbacks{};
        callbacks.on_color_trans_done = notify_flush_ready;
        ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(panel, &callbacks, display),
                            kTag, "register DPI callbacks failed");

        esp_timer_create_args_t tick_args{};
        tick_args.callback = lvgl_tick;
        tick_args.name = "deos_lv_tick";
        ESP_RETURN_ON_ERROR(esp_timer_create(&tick_args, &tick_timer), kTag,
                            "create LVGL tick timer failed");
        ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, kLvglTickMs * 1000), kTag,
                            "start LVGL tick timer failed");

        if (!ui::UiRuntime::instance().initialize(display)) {
            ESP_LOGE(kTag, "attach UI runtime failed");
            return ESP_ERR_NO_MEM;
        }

        const BaseType_t task_created = xTaskCreate(lvgl_task,
                                                     "deos_lvgl",
                                                     kLvglTaskStack,
                                                     nullptr,
                                                     kLvglTaskPriority,
                                                     &lvgl_task_handle);
        if (task_created != pdPASS) {
            ESP_LOGE(kTag, "create LVGL task failed");
            return ESP_ERR_NO_MEM;
        }
        return ESP_OK;
    }

    esp_err_t initialize() {
        if (initialized) {
            return ESP_OK;
        }

        ESP_LOGI(kTag, "Initializing 720x720 ST7703 display resource");
        ESP_RETURN_ON_ERROR(init_backlight(), kTag, "backlight init failed");
        ESP_RETURN_ON_ERROR(set_brightness(0), kTag, "backlight off failed");
        ESP_RETURN_ON_ERROR(acquire_power(), kTag, "display power init failed");
        ESP_RETURN_ON_ERROR(init_panel(), kTag, "display panel init failed");
        ESP_RETURN_ON_ERROR(init_lvgl(), kTag, "LVGL init failed");

        initialized = true;
        ESP_LOGI(kTag, "Display resource ready");
        return ESP_OK;
    }
};

DisplayController::DisplayController(DevicePreferences& preferences)
    : preferences_(preferences), impl_(std::make_unique<Impl>()) {}
DisplayController::~DisplayController() = default;

bool DisplayController::supports(std::string_view kind) const {
    return kind == "Display";
}

ResourceStatus DisplayController::reconcile(const Resource& desired,
                                            const AppliedResource*) {
    const esp_err_t init_result = impl_->initialize();
    if (init_result != ESP_OK) {
        return {Phase::Error,
                std::string("display init failed: ") + esp_err_to_name(init_result),
                {}};
    }

    const int brightness = parse_brightness(desired);
    const esp_err_t brightness_result = impl_->set_brightness(brightness);
    if (brightness_result != ESP_OK) {
        return {Phase::Error,
                std::string("brightness update failed: ") + esp_err_to_name(brightness_result),
                {}};
    }

    if (!preferences_.set_brightness(brightness)) {
        ESP_LOGW(kTag, "brightness applied but preference persistence failed");
    }

    return {Phase::Ready,
            "ST7703 + LVGL runtime ready",
            {
                {"panel", "st7703"},
                {"resolution", "720x720"},
                {"format", "rgb565"},
                {"renderer", "lvgl-9.5"},
                {"brightness", std::to_string(impl_->brightness)},
            }};
}

ResourceStatus DisplayController::remove(const AppliedResource&) {
    if (impl_->initialized) {
        (void)impl_->set_brightness(0);
    }
    return {Phase::Error,
            "hot removal is not implemented; reboot after removing Display resource",
            {{"brightness", "0"}}};
}

}  // namespace deos::platform
