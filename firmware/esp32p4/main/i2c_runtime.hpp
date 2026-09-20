// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

namespace deos::platform {

class I2cRuntime final {
public:
    static I2cRuntime& instance();

    esp_err_t initialize();
    bool ready() const noexcept;
    i2c_master_bus_handle_t handle() const noexcept;

private:
    I2cRuntime() = default;

    i2c_master_bus_handle_t bus_{nullptr};
};

}  // namespace deos::platform
