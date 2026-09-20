// SPDX-License-Identifier: Apache-2.0

#include "update_controller.hpp"

#include "network_controller.hpp"

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace deos::platform {
namespace {

constexpr char kTag[] = "deos-update";
constexpr size_t kChunkSize = 4096;

void reboot_after_ota(void*) {
    vTaskDelay(pdMS_TO_TICKS(900));
    esp_restart();
}

}  // namespace

struct UpdateController::Impl {
    NetworkController& network;
    bool registered{false};

    explicit Impl(NetworkController& network_controller)
        : network(network_controller) {}

    bool authorized(httpd_req_t* req) const {
        const std::string& expected = network.api_token();
        if (expected.empty()) {
            return false;
        }

        const size_t len = httpd_req_get_hdr_value_len(req, "X-DEOS-Token");
        if (len == 0 || len > 128) {
            return false;
        }

        std::string value(len + 1, '\0');
        if (httpd_req_get_hdr_value_str(
                req, "X-DEOS-Token", value.data(), value.size()) != ESP_OK) {
            return false;
        }
        value.resize(std::strlen(value.c_str()));
        return value == expected;
    }

    static esp_err_t ota_handler(httpd_req_t* req) {
        auto* self = static_cast<Impl*>(req->user_ctx);
        if (self == nullptr || !self->authorized(req)) {
            return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "invalid token");
        }

        if (req->content_len <= 0) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty firmware image");
        }

        const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
        if (update_partition == nullptr) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no OTA partition");
        }

        if (static_cast<size_t>(req->content_len) > update_partition->size) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "firmware image is too large");
        }

        ESP_LOGI(kTag,
                 "OTA begin: target=%s offset=0x%08lx size=%d",
                 update_partition->label,
                 static_cast<unsigned long>(update_partition->address),
                 req->content_len);

        esp_ota_handle_t handle = 0;
        esp_err_t err = esp_ota_begin(
            update_partition,
            static_cast<size_t>(req->content_len),
            &handle);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "esp_ota_begin failed: %s", esp_err_to_name(err));
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        }

        std::array<uint8_t, kChunkSize> buffer{};
        int remaining = req->content_len;
        size_t written = 0;

        while (remaining > 0) {
            const size_t want = std::min(
                buffer.size(),
                static_cast<size_t>(remaining));
            const int received = httpd_req_recv(
                req,
                reinterpret_cast<char*>(buffer.data()),
                want);

            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            if (received <= 0) {
                ESP_LOGE(kTag, "OTA socket receive failed after %u bytes",
                         static_cast<unsigned>(written));
                (void)esp_ota_abort(handle);
                return ESP_FAIL;
            }

            err = esp_ota_write(handle, buffer.data(), static_cast<size_t>(received));
            if (err != ESP_OK) {
                ESP_LOGE(kTag, "esp_ota_write failed: %s", esp_err_to_name(err));
                (void)esp_ota_abort(handle);
                return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            }

            written += static_cast<size_t>(received);
            remaining -= received;
        }

        err = esp_ota_end(handle);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "esp_ota_end failed: %s", esp_err_to_name(err));
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "firmware validation failed");
        }

        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "set boot partition failed: %s", esp_err_to_name(err));
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "boot switch failed");
        }

        char json[192]{};
        std::snprintf(
            json,
            sizeof(json),
            "{\"accepted\":true,\"bytes\":%u,\"slot\":\"%s\",\"rebooting\":true}",
            static_cast<unsigned>(written),
            update_partition->label);

        ESP_LOGI(kTag, "OTA verified: %u bytes -> %s",
                 static_cast<unsigned>(written), update_partition->label);

        httpd_resp_set_type(req, "application/json");
        const esp_err_t response = httpd_resp_sendstr(req, json);
        xTaskCreate(reboot_after_ota, "deos-ota-reboot", 2048, nullptr, 5, nullptr);
        return response;
    }

    esp_err_t register_endpoint() {
        if (registered) {
            return ESP_OK;
        }
        if (network.server() == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }

        httpd_uri_t ota{};
        ota.uri = "/api/v1/ota";
        ota.method = HTTP_POST;
        ota.handler = ota_handler;
        ota.user_ctx = this;

        ESP_RETURN_ON_ERROR(
            httpd_register_uri_handler(network.server(), &ota),
            kTag,
            "register OTA endpoint failed");

        registered = true;
        ESP_LOGI(kTag, "OTA endpoint armed: POST /api/v1/ota");
        return ESP_OK;
    }
};

UpdateController::UpdateController(NetworkController& network)
    : impl_(std::make_unique<Impl>(network)) {}

UpdateController::~UpdateController() = default;

bool UpdateController::supports(std::string_view kind) const {
    return kind == "Update";
}

ResourceStatus UpdateController::reconcile(const Resource&,
                                           const AppliedResource*) {
    const esp_err_t err = impl_->register_endpoint();
    if (err == ESP_ERR_INVALID_STATE) {
        return {Phase::Waiting, "network control server is not ready", {}};
    }
    if (err != ESP_OK) {
        return {
            Phase::Error,
            std::string("OTA service failed: ") + esp_err_to_name(err),
            {}
        };
    }

    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);

    return {
        Phase::Ready,
        "A/B OTA service ready",
        {
            {"endpoint", "/api/v1/ota"},
            {"auth", "X-DEOS-Token"},
            {"running", running != nullptr ? running->label : "unknown"},
            {"next", next != nullptr ? next->label : "unknown"},
            {"transport", "lan-http-dev"},
        }
    };
}

ResourceStatus UpdateController::remove(const AppliedResource&) {
    return {
        Phase::Error,
        "hot OTA service removal is not implemented",
        {}
    };
}

}  // namespace deos::platform
