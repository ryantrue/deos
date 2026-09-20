// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "lvgl.h"

#include <memory>

namespace deos::platform {
class NetworkController;
class StorageController;
}

namespace deos::ui {

class ShellUi final {
public:
    ShellUi(lv_display_t* display,
            platform::NetworkController& network,
            platform::StorageController& storage);
    ~ShellUi();

    ShellUi(const ShellUi&) = delete;
    ShellUi& operator=(const ShellUi&) = delete;

    void create();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace deos::ui
