// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "deos/core/action.hpp"
#include "deos/core/controller.hpp"
#include "deos/core/state.hpp"

#include "esp_http_server.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace deos::platform {

struct NetworkSnapshot {
    bool initialized{false};
    bool connected{false};
    bool provisioning{false};
    std::string ssid;
    std::string ip;
    std::string setup_ssid;
    std::string setup_password;
};

struct WifiScanEntry {
    std::string ssid;
    int rssi{0};
    int channel{0};
    bool secured{false};
};

struct WifiScanSnapshot {
    bool scanning{false};
    std::string error;
    std::vector<WifiScanEntry> entries;
};

class NetworkController final : public Controller {
public:
    NetworkController(EntityRegistry& entities, ActionRegistry& actions);
    ~NetworkController() override;

    bool supports(std::string_view kind) const override;
    ResourceStatus reconcile(const Resource& desired,
                             const AppliedResource* current) override;
    ResourceStatus remove(const AppliedResource& current) override;

    httpd_handle_t server() const noexcept;
    bool connected() const noexcept;
    bool provisioning() const noexcept;
    std::string ip() const;
    std::string api_token() const;
    std::string setup_ssid() const;
    std::string setup_password() const;

    NetworkSnapshot snapshot() const;
    WifiScanSnapshot scan_snapshot() const;

    bool request_scan();
    bool configure_wifi_and_reboot(const std::string& ssid,
                                   const std::string& password);
    bool forget_wifi_and_reboot();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace deos::platform
