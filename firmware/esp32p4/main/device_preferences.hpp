// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace deos::platform {

class DevicePreferences final {
public:
    bool initialize();

    int brightness(int fallback) const;
    bool set_brightness(int value);

private:
    bool initialized_{false};
};

}  // namespace deos::platform
