// SPDX-License-Identifier: Apache-2.0

#include "network_controller.hpp"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

namespace deos::platform {
namespace {

constexpr char kTag[] = "deos-network";
constexpr char kNvsNamespace[] = "deos";
constexpr char kSsidKey[] = "wifi_ssid";
constexpr char kPassKey[] = "wifi_pass";
constexpr char kTokenKey[] = "api_token";
constexpr int kMaxRetries = 20;
constexpr size_t kMaxSetupBody = 768;

void restart_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
}

std::string hex_token() {
    std::array<uint8_t, 16> bytes{};
    for (size_t i = 0; i < bytes.size(); i += sizeof(uint32_t)) {
        const uint32_t value = esp_random();
        const size_t remaining = std::min(sizeof(value), bytes.size() - i);
        std::memcpy(bytes.data() + i, &value, remaining);
    }

    char out[33]{};
    for (size_t i = 0; i < bytes.size(); ++i) {
        std::snprintf(out + (i * 2), 3, "%02x", bytes[i]);
    }
    return out;
}

int from_hex(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (c >= 'a' && c <= 'f') {
        return 10 + c - 'a';
    }
    return -1;
}

std::string url_decode(std::string_view value) {
    std::string result;
    result.reserve(value.size());

    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            result.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size()) {
            const int hi = from_hex(value[i + 1]);
            const int lo = from_hex(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                result.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                result.push_back(value[i]);
            }
        } else {
            result.push_back(value[i]);
        }
    }
    return result;
}

std::string form_value(std::string_view body, std::string_view key) {
    const std::string prefix = std::string(key) + "=";
    size_t start = 0;
    while (start < body.size()) {
        const size_t end = body.find('&', start);
        const size_t len = (end == std::string_view::npos ? body.size() : end) - start;
        const std::string_view item = body.substr(start, len);
        if (item.substr(0, prefix.size()) == prefix) {
            return url_decode(item.substr(prefix.size()));
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return {};
}

}  // namespace

struct NetworkController::Impl {
    NetworkController* owner{nullptr};
    mutable SemaphoreHandle_t state_mutex{nullptr};
    httpd_handle_t server{nullptr};
    esp_netif_t* netif{nullptr};
    esp_event_handler_instance_t wifi_handler{nullptr};
    esp_event_handler_instance_t ip_handler{nullptr};
    std::string ip_address;
    std::string token;
    std::string setup_ssid;
    std::string setup_password;
    std::string stored_ssid;
    std::string stored_password;
    bool initialized{false};
    bool is_connected{false};
    bool is_provisioning{false};
    int retry_count{0};

    Impl() {
        state_mutex = xSemaphoreCreateMutex();
    }

    ~Impl() {
        if (state_mutex != nullptr) {
            vSemaphoreDelete(state_mutex);
        }
    }

    void lock_state() const {
        if (state_mutex != nullptr) {
            (void)xSemaphoreTake(state_mutex, portMAX_DELAY);
        }
    }

    void unlock_state() const {
        if (state_mutex != nullptr) {
            (void)xSemaphoreGive(state_mutex);
        }
    }

    static void wifi_event(void* arg,
                           esp_event_base_t event_base,
                           int32_t event_id,
                           void* event_data) {
        auto* self = static_cast<Impl*>(arg);
        if (self == nullptr) {
            return;
        }

        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(kTag, "station started, connecting to '%s'", self->stored_ssid.c_str());
            (void)esp_wifi_connect();
            return;
        }

        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
            self->lock_state();
            self->is_connected = false;
            self->ip_address.clear();
            self->unlock_state();
            if (!self->is_provisioning && self->retry_count < kMaxRetries) {
                ++self->retry_count;
                ESP_LOGW(kTag, "Wi-Fi disconnected, retry %d/%d",
                         self->retry_count, kMaxRetries);
                (void)esp_wifi_connect();
            } else if (!self->is_provisioning) {
                ESP_LOGE(kTag, "Wi-Fi connection retries exhausted");
            }
            return;
        }

        if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
            const auto* event = static_cast<const ip_event_got_ip_t*>(event_data);
            char ip[16]{};
            std::snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
            self->lock_state();
            self->ip_address = ip;
            self->is_connected = true;
            self->unlock_state();
            self->retry_count = 0;
            ESP_LOGI(kTag, "Wi-Fi connected: %s", self->ip_address.c_str());
        }
    }

    esp_err_t init_nvs() {
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_RETURN_ON_ERROR(nvs_flash_erase(), kTag, "NVS erase failed");
            err = nvs_flash_init();
        }
        return err;
    }

    std::string read_string(nvs_handle_t nvs, const char* key) {
        size_t len = 0;
        if (nvs_get_str(nvs, key, nullptr, &len) != ESP_OK || len == 0) {
            return {};
        }
        std::string result(len, '\0');
        if (nvs_get_str(nvs, key, result.data(), &len) != ESP_OK) {
            return {};
        }
        if (!result.empty() && result.back() == '\0') {
            result.pop_back();
        }
        return result;
    }

    esp_err_t load_configuration() {
        nvs_handle_t nvs = 0;
        ESP_RETURN_ON_ERROR(nvs_open(kNvsNamespace, NVS_READWRITE, &nvs), kTag, "open NVS failed");

        stored_ssid = read_string(nvs, kSsidKey);
        stored_password = read_string(nvs, kPassKey);
        token = read_string(nvs, kTokenKey);

        if (token.empty()) {
            token = hex_token();
            const esp_err_t token_err = nvs_set_str(nvs, kTokenKey, token.c_str());
            if (token_err != ESP_OK) {
                nvs_close(nvs);
                return token_err;
            }
            ESP_RETURN_ON_ERROR(nvs_commit(nvs), kTag, "commit API token failed");
        }

        nvs_close(nvs);
        return ESP_OK;
    }

    esp_err_t clear_wifi_profile() {
        nvs_handle_t nvs = 0;
        ESP_RETURN_ON_ERROR(nvs_open(kNvsNamespace, NVS_READWRITE, &nvs), kTag, "open NVS failed");

        esp_err_t err = nvs_erase_key(nvs, kSsidKey);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
        if (err == ESP_OK) {
            esp_err_t pass_err = nvs_erase_key(nvs, kPassKey);
            if (pass_err != ESP_OK && pass_err != ESP_ERR_NVS_NOT_FOUND) {
                err = pass_err;
            }
        }
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);

        if (err == ESP_OK) {
            stored_ssid.clear();
            stored_password.clear();
        }
        return err;
    }

    esp_err_t save_wifi(const std::string& ssid, const std::string& password) {
        nvs_handle_t nvs = 0;
        ESP_RETURN_ON_ERROR(nvs_open(kNvsNamespace, NVS_READWRITE, &nvs), kTag, "open NVS failed");

        esp_err_t err = nvs_set_str(nvs, kSsidKey, ssid.c_str());
        if (err == ESP_OK) {
            err = nvs_set_str(nvs, kPassKey, password.c_str());
        }
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
        return err;
    }

    void derive_setup_credentials() {
        uint8_t mac[6]{};
        ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));

        char ssid[32]{};
        std::snprintf(ssid, sizeof(ssid), "DEOS-SETUP-%02X%02X", mac[4], mac[5]);
        setup_ssid = ssid;

        if (token.size() >= 12) {
            setup_password = "deos-" + token.substr(0, 8);
        } else {
            setup_password = "deos-setup";
        }
    }

    bool authorized(httpd_req_t* req) const {
        if (token.empty()) {
            return false;
        }

        const size_t len = httpd_req_get_hdr_value_len(req, "X-DEOS-Token");
        if (len == 0 || len > 128) {
            return false;
        }

        std::string supplied(len + 1, '\0');
        if (httpd_req_get_hdr_value_str(
                req, "X-DEOS-Token", supplied.data(), supplied.size()) != ESP_OK) {
            return false;
        }
        supplied.resize(std::strlen(supplied.c_str()));
        return supplied == token;
    }

    static esp_err_t setup_page(httpd_req_t* req) {
        static constexpr char kHtml[] =
            "<!doctype html><html><head><meta name='viewport' content='width=device-width'>"
            "<title>DEOS Setup</title><style>"
            "body{font-family:system-ui;background:#0b0e12;color:#eef2f6;max-width:520px;margin:40px auto;padding:20px}"
            "input,button{box-sizing:border-box;width:100%;padding:14px;margin:8px 0;border-radius:12px;border:1px solid #343b46}"
            "input{background:#151a21;color:white}button{background:#2b78ff;color:white;font-weight:700}"
            "</style></head><body><h1>DEOS network setup</h1>"
            "<p>Connect this device to your Wi-Fi. Credentials are stored only in device NVS.</p>"
            "<form method='post' action='/setup'>"
            "<input name='ssid' placeholder='Wi-Fi SSID' maxlength='32' required>"
            "<input name='password' type='password' placeholder='Wi-Fi password' maxlength='63'>"
            "<button type='submit'>Save and reboot</button></form></body></html>";

        httpd_resp_set_type(req, "text/html");
        return httpd_resp_send(req, kHtml, HTTPD_RESP_USE_STRLEN);
    }

    static esp_err_t setup_submit(httpd_req_t* req) {
        auto* self = static_cast<Impl*>(req->user_ctx);
        if (self == nullptr || req->content_len == 0 || req->content_len > kMaxSetupBody) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid request");
        }

        std::string body(req->content_len, '\0');
        size_t received = 0;
        while (received < body.size()) {
            const int n = httpd_req_recv(
                req, body.data() + received, body.size() - received);
            if (n == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            if (n <= 0) {
                return ESP_FAIL;
            }
            received += static_cast<size_t>(n);
        }

        const std::string ssid = form_value(body, "ssid");
        const std::string password = form_value(body, "password");
        if (ssid.empty() || ssid.size() > 32 || password.size() > 63) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid SSID or password");
        }

        const esp_err_t err = self->save_wifi(ssid, password);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "saving Wi-Fi profile failed: %s", esp_err_to_name(err));
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to save profile");
        }

        static constexpr char kSaved[] =
            "<html><body style='font-family:system-ui;background:#0b0e12;color:#fff'>"
            "<h2>Saved</h2><p>DEOS is rebooting and will connect to your network.</p>"
            "</body></html>";
        httpd_resp_set_type(req, "text/html");
        const esp_err_t response = httpd_resp_send(req, kSaved, HTTPD_RESP_USE_STRLEN);
        xTaskCreate(restart_task, "deos-restart", 2048, nullptr, 5, nullptr);
        return response;
    }

    static esp_err_t status_api(httpd_req_t* req) {
        auto* self = static_cast<Impl*>(req->user_ctx);
        if (self == nullptr) {
            return ESP_FAIL;
        }

        char json[512]{};
        std::snprintf(
            json,
            sizeof(json),
            "{\"device\":\"deos\",\"network\":{\"mode\":\"%s\","
            "\"connected\":%s,\"ip\":\"%s\",\"ssid\":\"%s\"},"
            "\"remote\":{\"ota\":true,\"auth\":\"token\"}}",
            self->is_provisioning ? "setup-ap" : "station",
            self->is_connected ? "true" : "false",
            self->ip_address.c_str(),
            self->is_provisioning ? self->setup_ssid.c_str() : self->stored_ssid.c_str());

        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, json);
    }

    static esp_err_t reboot_api(httpd_req_t* req) {
        auto* self = static_cast<Impl*>(req->user_ctx);
        if (self == nullptr || !self->authorized(req)) {
            return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "invalid token");
        }

        httpd_resp_set_type(req, "application/json");
        const esp_err_t response = httpd_resp_sendstr(req, "{\"rebooting\":true}");
        xTaskCreate(restart_task, "deos-restart", 2048, nullptr, 5, nullptr);
        return response;
    }

    static esp_err_t root_page(httpd_req_t* req) {
        auto* self = static_cast<Impl*>(req->user_ctx);
        if (self != nullptr && self->is_provisioning) {
            return setup_page(req);
        }

        static constexpr char kHtml[] =
            "<!doctype html><html><head><meta name='viewport' content='width=device-width'>"
            "<title>DEOS</title></head><body style='font-family:system-ui;background:#0b0e12;color:#fff'>"
            "<h1>DEOS</h1><p>Device control plane is online.</p>"
            "<p><code>GET /api/v1/status</code></p></body></html>";
        httpd_resp_set_type(req, "text/html");
        return httpd_resp_send(req, kHtml, HTTPD_RESP_USE_STRLEN);
    }

    esp_err_t start_http_server() {
        if (server != nullptr) {
            return ESP_OK;
        }

        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 80;
        config.max_uri_handlers = 12;
        ESP_RETURN_ON_ERROR(httpd_start(&server, &config), kTag, "HTTP server start failed");

        httpd_uri_t root{};
        root.uri = "/";
        root.method = HTTP_GET;
        root.handler = root_page;
        root.user_ctx = this;
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &root), kTag, "register root failed");

        httpd_uri_t setup_get{};
        setup_get.uri = "/setup";
        setup_get.method = HTTP_GET;
        setup_get.handler = setup_page;
        setup_get.user_ctx = this;
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &setup_get), kTag, "register setup GET failed");

        httpd_uri_t setup_post{};
        setup_post.uri = "/setup";
        setup_post.method = HTTP_POST;
        setup_post.handler = setup_submit;
        setup_post.user_ctx = this;
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &setup_post), kTag, "register setup POST failed");

        httpd_uri_t status{};
        status.uri = "/api/v1/status";
        status.method = HTTP_GET;
        status.handler = status_api;
        status.user_ctx = this;
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &status), kTag, "register status API failed");

        httpd_uri_t reboot{};
        reboot.uri = "/api/v1/reboot";
        reboot.method = HTTP_POST;
        reboot.handler = reboot_api;
        reboot.user_ctx = this;
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &reboot), kTag, "register reboot API failed");

        return ESP_OK;
    }

    esp_err_t start_mdns() {
        esp_err_t err = mdns_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        ESP_RETURN_ON_ERROR(mdns_hostname_set("deos"), kTag, "mDNS hostname failed");
        ESP_RETURN_ON_ERROR(mdns_instance_name_set("DEOS"), kTag, "mDNS instance failed");
        (void)mdns_service_add("DEOS control plane", "_deos", "_tcp", 80, nullptr, 0);
        return ESP_OK;
    }

    esp_err_t start_setup_ap() {
        is_provisioning = true;
        derive_setup_credentials();

        netif = esp_netif_create_default_wifi_ap();
        if (netif == nullptr) {
            return ESP_ERR_NO_MEM;
        }

        wifi_config_t config{};
        std::snprintf(reinterpret_cast<char*>(config.ap.ssid),
                      sizeof(config.ap.ssid), "%s", setup_ssid.c_str());
        std::snprintf(reinterpret_cast<char*>(config.ap.password),
                      sizeof(config.ap.password), "%s", setup_password.c_str());
        config.ap.ssid_len = static_cast<uint8_t>(setup_ssid.size());
        config.ap.channel = 1;
        config.ap.max_connection = 4;
        config.ap.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), kTag, "set AP mode failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &config), kTag, "set AP config failed");
        ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "start setup AP failed");

        ip_address = "192.168.4.1";
        ESP_LOGW(kTag, "No Wi-Fi profile found");
        ESP_LOGW(kTag, "Setup AP: %s", setup_ssid.c_str());
        ESP_LOGW(kTag, "Setup password: %s", setup_password.c_str());
        ESP_LOGW(kTag, "Open http://192.168.4.1/ after connecting");
        return ESP_OK;
    }

    esp_err_t start_station() {
        is_provisioning = false;

        netif = esp_netif_create_default_wifi_sta();
        if (netif == nullptr) {
            return ESP_ERR_NO_MEM;
        }

        ESP_RETURN_ON_ERROR(
            esp_event_handler_instance_register(
                WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, this, &wifi_handler),
            kTag,
            "register Wi-Fi event handler failed");
        ESP_RETURN_ON_ERROR(
            esp_event_handler_instance_register(
                IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, this, &ip_handler),
            kTag,
            "register IP event handler failed");

        wifi_config_t config{};
        std::snprintf(reinterpret_cast<char*>(config.sta.ssid),
                      sizeof(config.sta.ssid), "%s", stored_ssid.c_str());
        std::snprintf(reinterpret_cast<char*>(config.sta.password),
                      sizeof(config.sta.password), "%s", stored_password.c_str());
        config.sta.threshold.authmode = WIFI_AUTH_OPEN;

        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), kTag, "set STA mode failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), kTag, "set STA config failed");
        ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "start station failed");

        ESP_LOGI(kTag, "Station profile loaded: %s", stored_ssid.c_str());
        return ESP_OK;
    }

    esp_err_t initialize() {
        if (initialized) {
            return ESP_OK;
        }

        ESP_LOGI(kTag, "Initializing ESP32-C6 Wi-Fi coprocessor");
        int hosted_result = esp_hosted_init();
        if (hosted_result != ESP_OK) {
            ESP_LOGE(kTag, "esp_hosted_init failed: %d", hosted_result);
            return static_cast<esp_err_t>(hosted_result);
        }
        hosted_result = esp_hosted_connect_to_slave();
        if (hosted_result != ESP_OK) {
            ESP_LOGE(kTag, "esp_hosted_connect_to_slave failed: %d", hosted_result);
            return static_cast<esp_err_t>(hosted_result);
        }
        ESP_LOGI(kTag, "ESP32-C6 transport ready");

        ESP_RETURN_ON_ERROR(init_nvs(), kTag, "NVS init failed");
        ESP_RETURN_ON_ERROR(load_configuration(), kTag, "network config load failed");

        ESP_RETURN_ON_ERROR(esp_netif_init(), kTag, "esp_netif init failed");
        esp_err_t event_err = esp_event_loop_create_default();
        if (event_err != ESP_OK && event_err != ESP_ERR_INVALID_STATE) {
            return event_err;
        }

        wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init), kTag, "Wi-Fi init failed");
        (void)esp_wifi_set_storage(WIFI_STORAGE_RAM);

        if (stored_ssid.empty()) {
            ESP_RETURN_ON_ERROR(start_setup_ap(), kTag, "setup AP failed");
        } else {
            ESP_RETURN_ON_ERROR(start_station(), kTag, "station start failed");
        }

        ESP_RETURN_ON_ERROR(start_http_server(), kTag, "control HTTP server failed");
        ESP_RETURN_ON_ERROR(start_mdns(), kTag, "mDNS start failed");

        initialized = true;

        ESP_LOGI(kTag, "DEOS API token: %s", token.c_str());
        ESP_LOGI(kTag, "Control plane: http://deos.local/");
        return ESP_OK;
    }
};

NetworkController::NetworkController() : impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
}

NetworkController::~NetworkController() = default;

bool NetworkController::supports(std::string_view kind) const {
    return kind == "Network";
}

ResourceStatus NetworkController::reconcile(const Resource&,
                                            const AppliedResource*) {
    const esp_err_t err = impl_->initialize();
    if (err != ESP_OK) {
        return {
            Phase::Error,
            std::string("network init failed: ") + esp_err_to_name(err),
            {}
        };
    }

    return {
        Phase::Ready,
        impl_->is_provisioning ? "Wi-Fi setup AP ready" : "Wi-Fi station active",
        {
            {"transport", "esp32c6-sdio"},
            {"mode", impl_->is_provisioning ? "setup-ap" : "station"},
            {"ssid", impl_->is_provisioning ? impl_->setup_ssid : impl_->stored_ssid},
            {"ip", impl_->ip_address},
            {"control", "http://deos.local/"},
        }
    };
}

ResourceStatus NetworkController::remove(const AppliedResource&) {
    return {
        Phase::Error,
        "hot network removal is not implemented",
        {}
    };
}

httpd_handle_t NetworkController::server() const noexcept {
    return impl_->server;
}

bool NetworkController::connected() const noexcept {
    return impl_->is_connected;
}

bool NetworkController::provisioning() const noexcept {
    return impl_->is_provisioning;
}

const std::string& NetworkController::ip() const noexcept {
    return impl_->ip_address;
}

const std::string& NetworkController::api_token() const noexcept {
    return impl_->token;
}

const std::string& NetworkController::setup_ssid() const noexcept {
    return impl_->setup_ssid;
}

const std::string& NetworkController::setup_password() const noexcept {
    return impl_->setup_password;
}

NetworkSnapshot NetworkController::snapshot() const {
    NetworkSnapshot result;
    impl_->lock_state();
    result.initialized = impl_->initialized;
    result.connected = impl_->is_connected;
    result.provisioning = impl_->is_provisioning;
    result.ssid = impl_->is_provisioning ? impl_->setup_ssid : impl_->stored_ssid;
    result.ip = impl_->ip_address;
    result.setup_ssid = impl_->setup_ssid;
    result.setup_password = impl_->setup_password;
    impl_->unlock_state();
    return result;
}

bool NetworkController::forget_wifi_and_reboot() {
    if (!impl_->initialized || impl_->is_provisioning) {
        return false;
    }

    const esp_err_t err = impl_->clear_wifi_profile();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "forget Wi-Fi profile failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGW(kTag, "Wi-Fi profile removed; rebooting into provisioning");
    return xTaskCreate(restart_task, "deos-net-reset", 2048, nullptr, 5, nullptr) == pdPASS;
}

}  // namespace deos::platform


