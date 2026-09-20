// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "deos/core/controller.hpp"

#include "esp_http_server.h"

#include <memory>
#include <string>
#include <string_view>

namespace deos::platform {

class NetworkController final : public Controller {
public:
    NetworkController();
    ~NetworkController() override;

    bool supports(std::string_view kind) const override;
    ResourceStatus reconcile(const Resource& desired,
                             const AppliedResource* current) override;
    ResourceStatus remove(const AppliedResource& current) override;

    httpd_handle_t server() const noexcept;
    bool connected() const noexcept;
    bool provisioning() const noexcept;
    const std::string& ip() const noexcept;
    const std::string& api_token() const noexcept;
    const std::string& setup_ssid() const noexcept;
    const std::string& setup_password() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace deos::platform
