// SPDX-License-Identifier: Apache-2.0

#include "touch_controller.hpp"

#include "i2c_runtime.hpp"
#include "ui_runtime.hpp"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "lvgl.h"

#include <memory>
#include <string>

namespace deos::platform {
namespace {

constexpr char kTag[] = "deos-touch";
constexpr uint16_t kWidth = 720;
constexpr uint16_t kHeight = 720;
constexpr uint8_t kPrimaryAddress = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
constexpr uint8_t kBackupAddress = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
constexpr uint32_t kI2cHz = 400000;

}  // namespace

struct TouchController::Impl {
    esp_lcd_panel_io_handle_t io{nullptr};
    esp_lcd_touch_handle_t touch{nullptr};
    lv_indev_t* indev{nullptr};
    uint8_t address{0};
    bool initialized{false};

    static void read(lv_indev_t* indev_handle, lv_indev_data_t* data) {
        auto* self = static_cast<Impl*>(lv_indev_get_user_data(indev_handle));
        if (self == nullptr || self->touch == nullptr) {
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }

        if (esp_lcd_touch_read_data(self->touch) != ESP_OK) {
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }

        esp_lcd_touch_point_data_t point{};
        uint8_t count = 0;
        if (esp_lcd_touch_get_data(self->touch, &point, &count, 1) == ESP_OK && count > 0) {
            data->point.x = static_cast<int32_t>(point.x);
            data->point.y = static_cast<int32_t>(point.y);
            data->state = LV_INDEV_STATE_PRESSED;
        } else {
            data->state = LV_INDEV_STATE_RELEASED;
        }
    }

    esp_err_t initialize() {
        if (initialized) {
            return ESP_OK;
        }

        auto& ui_runtime = ui::UiRuntime::instance();
        if (!ui_runtime.ready()) {
            return ESP_ERR_INVALID_STATE;
        }

        auto& i2c = I2cRuntime::instance();
        ESP_RETURN_ON_ERROR(i2c.initialize(), kTag, "shared I2C init failed");

        if (i2c_master_probe(i2c.handle(), kPrimaryAddress, 100) == ESP_OK) {
            address = kPrimaryAddress;
        } else if (i2c_master_probe(i2c.handle(), kBackupAddress, 100) == ESP_OK) {
            address = kBackupAddress;
        } else {
            ESP_LOGE(kTag, "GT911 not found at 0x%02X or 0x%02X",
                     kPrimaryAddress, kBackupAddress);
            return ESP_ERR_NOT_FOUND;
        }

        ESP_LOGI(kTag, "GT911 found at 0x%02X", address);

        esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        io_config.dev_addr = address;
        io_config.scl_speed_hz = kI2cHz;
        ESP_RETURN_ON_ERROR(
            esp_lcd_new_panel_io_i2c(i2c.handle(), &io_config, &io),
            kTag,
            "GT911 panel IO init failed");

        esp_lcd_touch_config_t touch_config{};
        touch_config.x_max = kWidth;
        touch_config.y_max = kHeight;

        // Waveshare's current BSP intentionally polls this board without
        // driving INT/RST. This avoids changing the strap-selected I2C address.
        touch_config.rst_gpio_num = GPIO_NUM_NC;
        touch_config.int_gpio_num = GPIO_NUM_NC;
        touch_config.levels.reset = 0;
        touch_config.levels.interrupt = 0;
        touch_config.flags.swap_xy = 0;
        touch_config.flags.mirror_x = 0;
        touch_config.flags.mirror_y = 0;

        ESP_RETURN_ON_ERROR(
            esp_lcd_touch_new_i2c_gt911(io, &touch_config, &touch),
            kTag,
            "GT911 driver init failed");

        {
            ui::UiLock lock(ui_runtime);
            if (!lock.locked()) {
                return ESP_ERR_TIMEOUT;
            }

            indev = lv_indev_create();
            if (indev == nullptr) {
                return ESP_ERR_NO_MEM;
            }
            lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
            lv_indev_set_display(indev, ui_runtime.display());
            lv_indev_set_user_data(indev, this);
            lv_indev_set_read_cb(indev, read);
        }

        initialized = true;
        ESP_LOGI(kTag, "Input/touch ready");
        return ESP_OK;
    }
};

TouchController::TouchController() : impl_(std::make_unique<Impl>()) {}
TouchController::~TouchController() = default;

bool TouchController::supports(std::string_view kind) const {
    return kind == "Input";
}

ResourceStatus TouchController::reconcile(const Resource&,
                                          const AppliedResource*) {
    const esp_err_t err = impl_->initialize();
    if (err == ESP_ERR_INVALID_STATE) {
        return {Phase::Waiting, "UI runtime is not ready", {}};
    }
    if (err != ESP_OK) {
        return {
            Phase::Error,
            std::string("touch init failed: ") + esp_err_to_name(err),
            {}
        };
    }

    return {
        Phase::Ready,
        "GT911 touch ready",
        {
            {"driver", "gt911"},
            {"transport", "i2c0-polling"},
            {"address", impl_->address == kPrimaryAddress ? "0x5d" : "0x14"},
            {"resolution", "720x720"},
        }
    };
}

ResourceStatus TouchController::remove(const AppliedResource&) {
    return {
        Phase::Error,
        "hot removal is not implemented; reboot after removing Input/touch",
        {}
    };
}

}  // namespace deos::platform
