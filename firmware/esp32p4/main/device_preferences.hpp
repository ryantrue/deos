// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace deos::platform {

enum class SetupStep : std::uint8_t {
    Welcome = 0,
    Network = 1,
    Storage = 2,
    Ready = 3,
};

class DevicePreferences final {
public:
    bool initialize();

    int brightness(int fallback) const;
    bool set_brightness(int value);

    bool setup_completed() const;
    bool set_setup_completed(bool completed);

    SetupStep setup_step() const;
    bool set_setup_step(SetupStep step);

    bool developer_mode() const;
    bool set_developer_mode(bool enabled);

private:
    bool initialized_{false};
};

}  // namespace deos::platform
