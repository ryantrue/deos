// SPDX-License-Identifier: Apache-2.0

#include "i2c_runtime.hpp"

#include "driver/gpio.h"
#include "esp_log.h"

namespace deos::platform {
namespace {
constexpr char kTag[] = "deos-i2c";
constexpr i2c_port_num_t kPort = I2C_NUM_0;
constexpr gpio_num_t kSda = GPIO_NUM_7;
constexpr gpio_num_t kScl = GPIO_NUM_8;
}

I2cRuntime& I2cRuntime::instance() {
    static I2cRuntime runtime;
    return runtime;
}

esp_err_t I2cRuntime::initialize() {
    if (bus_ != nullptr) {
        return ESP_OK;
    }

    i2c_master_bus_config_t config{};
    config.i2c_port = kPort;
    config.sda_io_num = kSda;
    config.scl_io_num = kScl;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;

    const esp_err_t err = i2c_new_master_bus(&config, &bus_);
    if (err == ESP_OK) {
        ESP_LOGI(kTag, "I2C0 ready: SDA=%d SCL=%d", static_cast<int>(kSda), static_cast<int>(kScl));
    } else {
        ESP_LOGE(kTag, "I2C init failed: %s", esp_err_to_name(err));
    }
    return err;
}

bool I2cRuntime::ready() const noexcept {
    return bus_ != nullptr;
}

i2c_master_bus_handle_t I2cRuntime::handle() const noexcept {
    return bus_;
}

}  // namespace deos::platform
