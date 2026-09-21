// SPDX-License-Identifier: Apache-2.0

#include "network_controller.hpp"

#include "device_preferences.hpp"

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace deos::platform {
namespace {

constexpr char kTag[] = "deos-network";
constexpr char kNvsNamespace[] = "deos";
constexpr char kSsidKey[] = "wifi_ssid";
constexpr char kPassKey[] = "wifi_pass";
constexpr char kTokenKey[] = "api_token";
constexpr int kMaxRetries = 20;
constexpr size_t kMaxSetupBody = 768;

bool valid_wifi_credentials(std::string_view ssid, std::string_view password) {
    if (ssid.empty() || ssid.size() > 32 || password.size() > 63) {
        return false;
    }

    // An empty password selects an open network. WPA/WPA2/WPA3 passphrases
    // accepted by this UI are 8..63 characters.
    return password.empty() || password.size() >= 8;
}

const char* ota_state_name(const esp_partition_t* partition) {
    if (partition == nullptr) {
        return "unknown";
    }

    esp_ota_img_states_t state{};
    if (esp_ota_get_state_partition(partition, &state) != ESP_OK) {
        return "undefined";
    }

    switch (state) {
        case ESP_OTA_IMG_NEW: return "new";
        case ESP_OTA_IMG_PENDING_VERIFY: return "pending-verify";
        case ESP_OTA_IMG_VALID: return "valid";
        case ESP_OTA_IMG_INVALID: return "invalid";
        case ESP_OTA_IMG_ABORTED: return "aborted";
        case ESP_OTA_IMG_UNDEFINED: return "undefined";
    }
    return "unknown";
}

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

cJSON* state_value_to_json(const StateValue& value) {
    return std::visit(
        [](const auto& typed) -> cJSON* {
            using T = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<T, bool>) {
                return cJSON_CreateBool(typed);
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return cJSON_CreateNumber(static_cast<double>(typed));
            } else if constexpr (std::is_same_v<T, double>) {
                return cJSON_CreateNumber(typed);
            } else {
                return cJSON_CreateString(typed.c_str());
            }
        },
        value);
}

bool json_to_state_value(const cJSON* item, StateValue& out) {
    if (cJSON_IsBool(item)) {
        out = cJSON_IsTrue(item);
        return true;
    }
    if (cJSON_IsNumber(item)) {
        const double value = item->valuedouble;
        if (!std::isfinite(value)) {
            return false;
        }
        const double min_i64 =
            static_cast<double>(std::numeric_limits<std::int64_t>::min());
        const double max_i64 =
            static_cast<double>(std::numeric_limits<std::int64_t>::max());
        if (value >= min_i64 && value <= max_i64 && std::trunc(value) == value) {
            out = static_cast<std::int64_t>(value);
        } else {
            out = value;
        }
        return true;
    }
    if (cJSON_IsString(item) && item->valuestring != nullptr) {
        out = std::string(item->valuestring);
        return true;
    }
    return false;
}

esp_err_t send_json(httpd_req_t* req, cJSON* root) {
    if (root == nullptr) {
        return httpd_resp_send_err(
            req, HTTPD_500_INTERNAL_SERVER_ERROR, "json allocation failed");
    }

    char* payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == nullptr) {
        return httpd_resp_send_err(
            req, HTTPD_500_INTERNAL_SERVER_ERROR, "json serialization failed");
    }

    httpd_resp_set_type(req, "application/json");
    const esp_err_t result = httpd_resp_sendstr(req, payload);
    cJSON_free(payload);
    return result;
}

}  // namespace

struct NetworkController::Impl {
    NetworkController* owner{nullptr};
    EntityRegistry& entities;
    ActionRegistry& actions;
    DevicePreferences& preferences;
    mutable SemaphoreHandle_t state_mutex{nullptr};
    mutable SemaphoreHandle_t scan_mutex{nullptr};
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
    bool scan_in_progress{false};
    std::string scan_error;
    std::vector<WifiScanEntry> scan_entries;

    Impl(EntityRegistry& entity_registry,
         ActionRegistry& action_registry,
         DevicePreferences& device_preferences)
        : entities(entity_registry),
          actions(action_registry),
          preferences(device_preferences) {
        state_mutex = xSemaphoreCreateMutex();
        scan_mutex = xSemaphoreCreateMutex();
    }

    ~Impl() {
        if (state_mutex != nullptr) {
            vSemaphoreDelete(state_mutex);
        }
        if (scan_mutex != nullptr) {
            vSemaphoreDelete(scan_mutex);
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

    void lock_scan() const {
        if (scan_mutex != nullptr) {
            (void)xSemaphoreTake(scan_mutex, portMAX_DELAY);
        }
    }

    void unlock_scan() const {
        if (scan_mutex != nullptr) {
            (void)xSemaphoreGive(scan_mutex);
        }
    }

    WifiScanSnapshot scan_snapshot_state() const {
        WifiScanSnapshot result;
        lock_scan();
        result.scanning = scan_in_progress;
        result.error = scan_error;
        result.entries = scan_entries;
        unlock_scan();
        return result;
    }

    NetworkSnapshot snapshot_state() const {
        NetworkSnapshot result;
        lock_state();
        result.initialized = initialized;
        result.connected = is_connected;
        result.provisioning = is_provisioning;
        result.ssid = is_provisioning ? setup_ssid : stored_ssid;
        result.ip = ip_address;
        result.setup_ssid = setup_ssid;
        result.setup_password = setup_password;
        unlock_state();
        return result;
    }

    std::string token_copy() const {
        lock_state();
        std::string copy = token;
        unlock_state();
        return copy;
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
            const NetworkSnapshot state = self->snapshot_state();
            if (!state.provisioning && !state.ssid.empty()) {
                ESP_LOGI(kTag, "station started, connecting to '%s'", state.ssid.c_str());
                (void)esp_wifi_connect();
            }
            return;
        }

        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
            self->lock_state();
            self->is_connected = false;
            self->ip_address.clear();
            self->unlock_state();
            (void)self->entities.set("network.connected", false);
            (void)self->entities.set("network.ip", std::string(""));
            const NetworkSnapshot state = self->snapshot_state();
            if (!state.provisioning && self->retry_count < kMaxRetries) {
                ++self->retry_count;
                ESP_LOGW(kTag, "Wi-Fi disconnected, retry %d/%d",
                         self->retry_count, kMaxRetries);
                (void)esp_wifi_connect();
            } else if (!state.provisioning) {
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
            (void)self->entities.set("network.ip", std::string(ip));
            (void)self->entities.set("network.connected", true);
            self->retry_count = 0;
            ESP_LOGI(kTag, "Wi-Fi connected: %s", ip);
        }
    }

    static void scan_worker(void* arg) {
        auto* self = static_cast<Impl*>(arg);
        if (self == nullptr) {
            vTaskDelete(nullptr);
            return;
        }

        self->lock_scan();
        self->scan_entries.clear();
        self->scan_error.clear();
        self->scan_in_progress = true;
        self->unlock_scan();

        wifi_scan_config_t config{};
        config.show_hidden = false;
        config.scan_type = WIFI_SCAN_TYPE_ACTIVE;

        esp_err_t err = esp_wifi_scan_start(&config, true);
        std::vector<WifiScanEntry> discovered;

        if (err == ESP_OK) {
            uint16_t count = 0;
            err = esp_wifi_scan_get_ap_num(&count);
            if (err == ESP_OK && count > 0) {
                constexpr uint16_t kMaxScanResults = 16;
                count = std::min<uint16_t>(count, kMaxScanResults);

                std::vector<wifi_ap_record_t> records(count);
                uint16_t returned = count;
                err = esp_wifi_scan_get_ap_records(&returned, records.data());

                if (err == ESP_OK) {
                    discovered.reserve(returned);
                    for (uint16_t i = 0; i < returned; ++i) {
                        const auto& record = records[i];
                        const char* ssid =
                            reinterpret_cast<const char*>(record.ssid);
                        if (ssid == nullptr || ssid[0] == '\0') {
                            continue;
                        }

                        const std::string name(ssid);
                        const bool duplicate = std::any_of(
                            discovered.begin(),
                            discovered.end(),
                            [&](const WifiScanEntry& item) {
                                return item.ssid == name;
                            });
                        if (duplicate) {
                            continue;
                        }

                        discovered.push_back({
                            name,
                            static_cast<int>(record.rssi),
                            static_cast<int>(record.primary),
                            record.authmode != WIFI_AUTH_OPEN,
                        });
                    }
                }
            }
        }

        if (err != ESP_OK) {
            (void)esp_wifi_clear_ap_list();
        }

        self->lock_scan();
        self->scan_entries = std::move(discovered);
        self->scan_error =
            err == ESP_OK ? std::string{} : std::string(esp_err_to_name(err));
        self->scan_in_progress = false;
        self->unlock_scan();

        if (err == ESP_OK) {
            ESP_LOGI(
                kTag,
                "Wi-Fi scan complete: %u network(s)",
                static_cast<unsigned>(self->scan_entries.size()));
        } else {
            ESP_LOGW(kTag, "Wi-Fi scan failed: %s", esp_err_to_name(err));
        }

        vTaskDelete(nullptr);
    }

    bool request_scan_async() {
        const NetworkSnapshot state = snapshot_state();
        if (!state.initialized) {
            return false;
        }

        lock_scan();
        if (scan_in_progress) {
            unlock_scan();
            return false;
        }
        scan_in_progress = true;
        scan_error.clear();
        unlock_scan();

        const BaseType_t created = xTaskCreate(
            scan_worker,
            "deos-wifi-scan",
            6144,
            this,
            4,
            nullptr);
        if (created != pdPASS) {
            lock_scan();
            scan_in_progress = false;
            scan_error = "could not start scan task";
            unlock_scan();
            return false;
        }
        return true;
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

        std::string loaded_ssid = read_string(nvs, kSsidKey);
        std::string loaded_password = read_string(nvs, kPassKey);
        std::string loaded_token = read_string(nvs, kTokenKey);

        if (loaded_token.empty()) {
            loaded_token = hex_token();
            const esp_err_t token_err =
                nvs_set_str(nvs, kTokenKey, loaded_token.c_str());
            if (token_err != ESP_OK) {
                nvs_close(nvs);
                return token_err;
            }
            ESP_RETURN_ON_ERROR(
                nvs_commit(nvs),
                kTag,
                "commit API token failed");
        }

        nvs_close(nvs);

        lock_state();
        stored_ssid = std::move(loaded_ssid);
        stored_password = std::move(loaded_password);
        token = std::move(loaded_token);
        unlock_state();
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
            lock_state();
            stored_ssid.clear();
            stored_password.clear();
            unlock_state();
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

        if (err == ESP_OK) {
            lock_state();
            stored_ssid = ssid;
            stored_password = password;
            unlock_state();
        }
        return err;
    }

    void derive_setup_credentials() {
        uint8_t mac[6]{};
        const esp_err_t mac_err = esp_wifi_get_mac(WIFI_IF_STA, mac);

        const std::string current_token = token_copy();
        char suffix[5]{};
        if (mac_err == ESP_OK) {
            std::snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
        } else if (current_token.size() >= 4) {
            const size_t start = current_token.size() - 4;
            for (size_t i = 0; i < 4; ++i) {
                suffix[i] = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(current_token[start + i])));
            }
            ESP_LOGW(
                kTag,
                "remote Wi-Fi MAC unavailable (%s); using persisted device identity",
                esp_err_to_name(mac_err));
        } else {
            const uint32_t fallback = esp_random();
            std::snprintf(suffix, sizeof(suffix), "%04X",
                          static_cast<unsigned>(fallback & 0xffffU));
            ESP_LOGW(
                kTag,
                "remote Wi-Fi MAC and persisted identity unavailable; using runtime suffix");
        }

        char ssid[32]{};
        std::snprintf(
            ssid,
            sizeof(ssid),
            "DEOS-SETUP-%s",
            suffix);

        std::string password =
            current_token.size() >= 12
                ? "deos-" + current_token.substr(0, 8)
                : "deos-setup";

        lock_state();
        setup_ssid = ssid;
        setup_password = std::move(password);
        unlock_state();
    }

    bool authorized(httpd_req_t* req) const {
        const std::string expected = token_copy();
        if (expected.empty()) {
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
        return supplied == expected;
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
        if (!valid_wifi_credentials(ssid, password)) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid SSID or password");
        }

        const esp_err_t err = self->save_wifi(ssid, password);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "saving Wi-Fi profile failed: %s", esp_err_to_name(err));
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to save profile");
        }

        if (!self->preferences.setup_completed()) {
            if (!self->preferences.set_setup_step(SetupStep::Storage)) {
                ESP_LOGE(kTag, "failed to persist onboarding resume step");
                return httpd_resp_send_err(
                    req,
                    HTTPD_500_INTERNAL_SERVER_ERROR,
                    "failed to save onboarding state");
            }
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

        const NetworkSnapshot state = self->snapshot_state();
        const esp_partition_t* running = esp_ota_get_running_partition();
        const esp_app_desc_t* app = esp_app_get_description();

        cJSON* root = cJSON_CreateObject();
        if (root == nullptr) {
            return httpd_resp_send_err(
                req, HTTPD_500_INTERNAL_SERVER_ERROR, "json allocation failed");
        }
        cJSON_AddStringToObject(root, "device", "deos");

        cJSON* network = cJSON_AddObjectToObject(root, "network");
        cJSON* firmware = cJSON_AddObjectToObject(root, "firmware");
        cJSON* remote = cJSON_AddObjectToObject(root, "remote");
        if (network == nullptr || firmware == nullptr || remote == nullptr) {
            cJSON_Delete(root);
            return httpd_resp_send_err(
                req, HTTPD_500_INTERNAL_SERVER_ERROR, "json allocation failed");
        }

        cJSON_AddStringToObject(
            network,
            "mode",
            state.provisioning ? "setup-ap" : "station");
        cJSON_AddBoolToObject(network, "connected", state.connected);
        cJSON_AddStringToObject(network, "ip", state.ip.c_str());

        cJSON_AddStringToObject(
            firmware,
            "version",
            app != nullptr ? app->version : "unknown");
        cJSON_AddStringToObject(
            firmware,
            "idf_version",
            app != nullptr ? app->idf_ver : "unknown");
        cJSON_AddStringToObject(
            firmware,
            "slot",
            running != nullptr ? running->label : "unknown");
        cJSON_AddStringToObject(firmware, "ota_state", ota_state_name(running));

        cJSON_AddBoolToObject(remote, "ota", true);
        cJSON_AddBoolToObject(remote, "state_actions", true);
        cJSON_AddStringToObject(remote, "auth", "token");
        return send_json(req, root);
    }

    static esp_err_t entities_api(httpd_req_t* req) {
        auto* self = static_cast<Impl*>(req->user_ctx);
        if (self == nullptr || !self->authorized(req)) {
            return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "invalid token");
        }

        cJSON* root = cJSON_CreateObject();
        if (root == nullptr) {
            return httpd_resp_send_err(
                req, HTTPD_500_INTERNAL_SERVER_ERROR, "json allocation failed");
        }
        cJSON* items = cJSON_AddArrayToObject(root, "entities");
        if (items == nullptr) {
            cJSON_Delete(root);
            return httpd_resp_send_err(
                req, HTTPD_500_INTERNAL_SERVER_ERROR, "json allocation failed");
        }

        for (const auto& snapshot : self->entities.list()) {
            cJSON* item = cJSON_CreateObject();
            if (item == nullptr) {
                cJSON_Delete(root);
                return httpd_resp_send_err(
                    req, HTTPD_500_INTERNAL_SERVER_ERROR, "json allocation failed");
            }
            cJSON_AddStringToObject(item, "id", snapshot.descriptor.id.c_str());
            cJSON_AddStringToObject(item, "name", snapshot.descriptor.name.c_str());
            cJSON_AddStringToObject(item, "source", snapshot.descriptor.source.c_str());
            cJSON_AddStringToObject(item, "unit", snapshot.descriptor.unit.c_str());
            cJSON_AddNumberToObject(
                item, "revision", static_cast<double>(snapshot.revision));
            cJSON* value = state_value_to_json(snapshot.value);
            if (value == nullptr) {
                cJSON_Delete(item);
                cJSON_Delete(root);
                return httpd_resp_send_err(
                    req, HTTPD_500_INTERNAL_SERVER_ERROR, "json allocation failed");
            }
            cJSON_AddItemToObject(item, "value", value);
            cJSON_AddItemToArray(items, item);
        }
        return send_json(req, root);
    }

    static esp_err_t actions_api(httpd_req_t* req) {
        auto* self = static_cast<Impl*>(req->user_ctx);
        if (self == nullptr || !self->authorized(req)) {
            return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "invalid token");
        }

        cJSON* root = cJSON_CreateObject();
        if (root == nullptr) {
            return httpd_resp_send_err(
                req, HTTPD_500_INTERNAL_SERVER_ERROR, "json allocation failed");
        }
        cJSON* items = cJSON_AddArrayToObject(root, "actions");
        if (items == nullptr) {
            cJSON_Delete(root);
            return httpd_resp_send_err(
                req, HTTPD_500_INTERNAL_SERVER_ERROR, "json allocation failed");
        }

        for (const auto& descriptor : self->actions.list()) {
            cJSON* item = cJSON_CreateObject();
            if (item == nullptr) {
                cJSON_Delete(root);
                return httpd_resp_send_err(
                    req, HTTPD_500_INTERNAL_SERVER_ERROR, "json allocation failed");
            }
            cJSON_AddStringToObject(item, "id", descriptor.id.c_str());
            cJSON_AddStringToObject(item, "name", descriptor.name.c_str());
            cJSON_AddStringToObject(item, "description", descriptor.description.c_str());
            cJSON_AddStringToObject(item, "capability", descriptor.capability.c_str());
            cJSON* parameters = cJSON_AddArrayToObject(item, "parameters");
            if (parameters == nullptr) {
                cJSON_Delete(item);
                cJSON_Delete(root);
                return httpd_resp_send_err(
                    req, HTTPD_500_INTERNAL_SERVER_ERROR, "json allocation failed");
            }
            for (const auto& parameter : descriptor.parameters) {
                cJSON* value = cJSON_CreateString(parameter.c_str());
                if (value == nullptr) {
                    cJSON_Delete(item);
                    cJSON_Delete(root);
                    return httpd_resp_send_err(
                        req, HTTPD_500_INTERNAL_SERVER_ERROR, "json allocation failed");
                }
                cJSON_AddItemToArray(parameters, value);
            }
            cJSON_AddItemToArray(items, item);
        }
        return send_json(req, root);
    }

    static esp_err_t invoke_action_api(httpd_req_t* req) {
        auto* self = static_cast<Impl*>(req->user_ctx);
        if (self == nullptr || !self->authorized(req)) {
            return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "invalid token");
        }
        if (req->content_len <= 0 || req->content_len > 4096) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid request body");
        }

        std::string body(static_cast<size_t>(req->content_len), '\0');
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

        cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
        if (root == nullptr) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        }

        const cJSON* id = cJSON_GetObjectItemCaseSensitive(root, "id");
        const cJSON* args = cJSON_GetObjectItemCaseSensitive(root, "args");
        if (!cJSON_IsString(id) || id->valuestring == nullptr ||
            (args != nullptr && !cJSON_IsObject(args))) {
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "id/args invalid");
        }

        StateValues arguments;
        if (args != nullptr) {
            const cJSON* entry = nullptr;
            cJSON_ArrayForEach(entry, args) {
                if (entry->string == nullptr) {
                    cJSON_Delete(root);
                    return httpd_resp_send_err(
                        req, HTTPD_400_BAD_REQUEST, "argument name missing");
                }
                StateValue value;
                if (!json_to_state_value(entry, value)) {
                    cJSON_Delete(root);
                    return httpd_resp_send_err(
                        req, HTTPD_400_BAD_REQUEST, "arguments must be scalar values");
                }
                arguments.emplace(entry->string, std::move(value));
            }
        }

        const std::string action_id(id->valuestring);
        cJSON_Delete(root);

        const ActionContext context{
            "remote-api",
            {
                "display.control",
                "storage.control",
                "network.control",
            },
        };
        const ActionResult result = self->actions.invoke(action_id, context, arguments);

        cJSON* response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "ok", result.ok);
        cJSON_AddStringToObject(response, "message", result.message.c_str());
        cJSON* output = cJSON_AddObjectToObject(response, "output");
        for (const auto& [key, value] : result.output) {
            cJSON* json_value = state_value_to_json(value);
            if (json_value != nullptr) {
                cJSON_AddItemToObject(output, key.c_str(), json_value);
            }
        }

        if (!result.ok) {
            httpd_resp_set_status(req, "409 Conflict");
        }
        return send_json(req, response);
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
        if (self != nullptr && self->snapshot_state().provisioning) {
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

        httpd_uri_t entities_uri{};
        entities_uri.uri = "/api/v1/entities";
        entities_uri.method = HTTP_GET;
        entities_uri.handler = entities_api;
        entities_uri.user_ctx = this;
        ESP_RETURN_ON_ERROR(
            httpd_register_uri_handler(server, &entities_uri),
            kTag,
            "register entities API failed");

        httpd_uri_t actions_uri{};
        actions_uri.uri = "/api/v1/actions";
        actions_uri.method = HTTP_GET;
        actions_uri.handler = actions_api;
        actions_uri.user_ctx = this;
        ESP_RETURN_ON_ERROR(
            httpd_register_uri_handler(server, &actions_uri),
            kTag,
            "register actions API failed");

        httpd_uri_t invoke_uri{};
        invoke_uri.uri = "/api/v1/action";
        invoke_uri.method = HTTP_POST;
        invoke_uri.handler = invoke_action_api;
        invoke_uri.user_ctx = this;
        ESP_RETURN_ON_ERROR(
            httpd_register_uri_handler(server, &invoke_uri),
            kTag,
            "register action invoke API failed");

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
        derive_setup_credentials();

        lock_state();
        is_provisioning = true;
        const std::string ap_ssid = setup_ssid;
        const std::string ap_password = setup_password;
        unlock_state();

        netif = esp_netif_create_default_wifi_ap();
        if (netif == nullptr) {
            return ESP_ERR_NO_MEM;
        }

        wifi_config_t config{};
        std::snprintf(reinterpret_cast<char*>(config.ap.ssid),
                      sizeof(config.ap.ssid), "%s", ap_ssid.c_str());
        std::snprintf(reinterpret_cast<char*>(config.ap.password),
                      sizeof(config.ap.password), "%s", ap_password.c_str());
        config.ap.ssid_len = static_cast<uint8_t>(ap_ssid.size());
        config.ap.channel = 1;
        config.ap.max_connection = 4;
        config.ap.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), kTag, "set AP+STA mode failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &config), kTag, "set AP config failed");
        ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "start setup AP failed");

        lock_state();
        ip_address = "192.168.4.1";
        is_connected = false;
        unlock_state();
        (void)entities.set("network.connected", false);
        (void)entities.set("network.ip", std::string("192.168.4.1"));
        (void)entities.set("network.mode", std::string("setup-ap"));
        (void)entities.set("network.ssid", ap_ssid);
        ESP_LOGW(kTag, "No Wi-Fi profile found");
        ESP_LOGW(kTag, "Setup AP: %s", ap_ssid.c_str());
        ESP_LOGW(kTag, "Setup password: %s", ap_password.c_str());
        ESP_LOGW(kTag, "Open http://192.168.4.1/ after connecting");
        return ESP_OK;
    }

    esp_err_t start_station() {
        lock_state();
        is_provisioning = false;
        is_connected = false;
        ip_address.clear();
        const std::string station_ssid = stored_ssid;
        const std::string station_password = stored_password;
        unlock_state();
        (void)entities.set("network.connected", false);
        (void)entities.set("network.ip", std::string(""));
        (void)entities.set("network.mode", std::string("station"));
        (void)entities.set("network.ssid", station_ssid);

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
                      sizeof(config.sta.ssid), "%s", station_ssid.c_str());
        std::snprintf(reinterpret_cast<char*>(config.sta.password),
                      sizeof(config.sta.password), "%s", station_password.c_str());
        config.sta.threshold.authmode = WIFI_AUTH_OPEN;

        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), kTag, "set STA mode failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), kTag, "set STA config failed");
        ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "start station failed");

        ESP_LOGI(kTag, "Station profile loaded: %s", station_ssid.c_str());
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

        if (snapshot_state().ssid.empty()) {
            ESP_RETURN_ON_ERROR(
                start_setup_ap(),
                kTag,
                "setup AP failed");
        } else {
            ESP_RETURN_ON_ERROR(
                start_station(),
                kTag,
                "station start failed");
        }

        ESP_RETURN_ON_ERROR(start_http_server(), kTag, "control HTTP server failed");
        ESP_RETURN_ON_ERROR(start_mdns(), kTag, "mDNS start failed");

        lock_state();
        initialized = true;
        unlock_state();

        const std::string log_token = token_copy();
        ESP_LOGI(kTag, "DEOS API token: %s", log_token.c_str());
        ESP_LOGI(kTag, "Control plane: http://deos.local/");
        return ESP_OK;
    }
};

NetworkController::NetworkController(
    EntityRegistry& entities,
    ActionRegistry& actions,
    DevicePreferences& preferences)
    : impl_(std::make_unique<Impl>(entities, actions, preferences)) {
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

    const NetworkSnapshot state = impl_->snapshot_state();
    return {
        Phase::Ready,
        state.provisioning ? "Wi-Fi setup AP ready" : "Wi-Fi station active",
        {
            {"transport", "esp32c6-sdio"},
            {"mode", state.provisioning ? "setup-ap" : "station"},
            {"ssid", state.ssid},
            {"ip", state.ip},
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
    return impl_->snapshot_state().connected;
}

bool NetworkController::provisioning() const noexcept {
    return impl_->snapshot_state().provisioning;
}

std::string NetworkController::ip() const {
    return impl_->snapshot_state().ip;
}

std::string NetworkController::api_token() const {
    return impl_->token_copy();
}

std::string NetworkController::setup_ssid() const {
    return impl_->snapshot_state().setup_ssid;
}

std::string NetworkController::setup_password() const {
    return impl_->snapshot_state().setup_password;
}

NetworkSnapshot NetworkController::snapshot() const {
    return impl_->snapshot_state();
}

WifiScanSnapshot NetworkController::scan_snapshot() const {
    return impl_->scan_snapshot_state();
}

bool NetworkController::request_scan() {
    return impl_->request_scan_async();
}

bool NetworkController::configure_wifi_and_reboot(
    const std::string& ssid,
    const std::string& password) {

    if (!impl_->snapshot_state().initialized ||
        !valid_wifi_credentials(ssid, password)) {
        return false;
    }

    const esp_err_t err = impl_->save_wifi(ssid, password);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "saving Wi-Fi profile failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(kTag, "Wi-Fi profile saved from local UI; reboot scheduled");
    return xTaskCreate(
               restart_task,
               "deos-net-config",
               2048,
               nullptr,
               5,
               nullptr) == pdPASS;
}

bool NetworkController::forget_wifi_and_reboot() {
    const NetworkSnapshot state = impl_->snapshot_state();
    if (!state.initialized || state.provisioning) {
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
