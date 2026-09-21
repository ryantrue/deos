// SPDX-License-Identifier: Apache-2.0

#include "storage_controller.hpp"

#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "diskio_sdmmc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <utility>

namespace deos::platform {
namespace {

constexpr char kTag[] = "deos-storage";
constexpr char kMountPoint[] = "/sdcard";
constexpr char kMarker[] = "/sdcard/DEOS/.volume";
constexpr uint32_t kWorkerStack = 6144;
constexpr UBaseType_t kWorkerPriority = 3;

constexpr const char* kDirectories[] = {
    "/sdcard/DEOS",
    "/sdcard/DEOS/Apps",
    "/sdcard/DEOS/AppData",
    "/sdcard/DEOS/Packages",
    "/sdcard/DEOS/Backups",
    "/sdcard/DEOS/Logs",
    "/sdcard/Media",
    "/sdcard/Media/Music",
    "/sdcard/Media/Pictures",
    "/sdcard/Media/Video",
    "/sdcard/Documents",
    "/sdcard/Downloads",
};

bool path_exists(const char* path) {
    struct stat info {};
    return stat(path, &info) == 0;
}

esp_err_t ensure_directory(const char* path) {
    if (mkdir(path, 0775) == 0) {
        return ESP_OK;
    }

    if (errno == EEXIST) {
        struct stat info {};
        if (stat(path, &info) == 0 && S_ISDIR(info.st_mode)) {
            return ESP_OK;
        }
        ESP_LOGE(kTag, "%s exists but is not a directory", path);
        return ESP_FAIL;
    }

    ESP_LOGE(kTag, "mkdir %s failed: %s", path, std::strerror(errno));
    return ESP_FAIL;
}

std::string card_display_name(const sdmmc_card_t* card) {
    if (card == nullptr) {
        return {};
    }
    char name[32]{};
    const uint64_t bytes =
        static_cast<uint64_t>(card->csd.capacity) *
        static_cast<uint64_t>(card->csd.sector_size);
    std::snprintf(name, sizeof(name), "%s %llu MB",
                  card->cid.name,
                  static_cast<unsigned long long>(bytes / (1024ULL * 1024ULL)));
    return name;
}

}  // namespace

const char* to_string(SdVolumeState state) noexcept {
    switch (state) {
        case SdVolumeState::Unknown: return "unknown";
        case SdVolumeState::Absent: return "absent";
        case SdVolumeState::Foreign: return "foreign";
        case SdVolumeState::Ready: return "ready";
        case SdVolumeState::NeedsFormat: return "needs-format";
        case SdVolumeState::Busy: return "busy";
        case SdVolumeState::Error: return "error";
    }
    return "unknown";
}

struct StorageController::Impl {
    EntityRegistry& entities;

    enum class Operation {
        None,
        Probe,
        Initialize,
        Format,
    };

    mutable SemaphoreHandle_t mutex{nullptr};
    sdmmc_card_t* card{nullptr};
    bool mounted{false};
    bool initialized{false};
    SdVolumeSnapshot status{};
    Operation operation{Operation::None};

    explicit Impl(EntityRegistry& entity_registry)
        : entities(entity_registry) {
        mutex = xSemaphoreCreateMutex();
        status.state = SdVolumeState::Unknown;
        status.message = "SD storage not probed yet";
    }

    ~Impl() {
        if (mounted && card != nullptr) {
            (void)esp_vfs_fat_sdcard_unmount(kMountPoint, card);
        }
        if (mutex != nullptr) {
            vSemaphoreDelete(mutex);
        }
    }

    void lock() const {
        if (mutex != nullptr) {
            (void)xSemaphoreTake(mutex, portMAX_DELAY);
        }
    }

    void unlock() const {
        if (mutex != nullptr) {
            (void)xSemaphoreGive(mutex);
        }
    }

    void set_status(SdVolumeState state,
                    std::string message,
                    std::string op = {}) {
        lock();
        status.state = state;
        status.message = std::move(message);
        status.operation = std::move(op);
        unlock();

        (void)entities.set("storage.sd.state", std::string(to_string(state)));
    }

    void update_capacity() {
        struct statvfs vfs {};
        uint64_t total = 0;
        uint64_t free = 0;
        if (statvfs(kMountPoint, &vfs) == 0) {
            total = static_cast<uint64_t>(vfs.f_blocks) * vfs.f_frsize;
            free = static_cast<uint64_t>(vfs.f_bavail) * vfs.f_frsize;
        }

        lock();
        status.total_bytes = total;
        status.free_bytes = free;
        status.card_name = card_display_name(card);
        unlock();

        const auto safe_total = static_cast<std::int64_t>(
            std::min<std::uint64_t>(
                total,
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())));
        const auto safe_free = static_cast<std::int64_t>(
            std::min<std::uint64_t>(
                free,
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())));
        (void)entities.set("storage.sd.total_bytes", safe_total);
        (void)entities.set("storage.sd.free_bytes", safe_free);
    }

    sdmmc_host_t host_config() const {
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.slot = SDMMC_HOST_SLOT_0;
        host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
        return host;
    }

    sdmmc_slot_config_t slot_config() const {
        sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
        // Waveshare ESP32-P4-WIFI6-Touch-LCD-4B routes the microSD socket
        // to SDMMC slot 0 IO-MUX pins. No GPIO matrix mapping is required.
        slot.width = 4;
        slot.cd = SDMMC_SLOT_NO_CD;
        slot.wp = SDMMC_SLOT_NO_WP;
        return slot;
    }

    esp_err_t mount(bool format_if_failed) {
        if (mounted) {
            return ESP_OK;
        }

        esp_vfs_fat_sdmmc_mount_config_t cfg{};
        cfg.format_if_mount_failed = format_if_failed;
        cfg.max_files = 12;
        cfg.allocation_unit_size = 64 * 1024;

        sdmmc_host_t host = host_config();
        sdmmc_slot_config_t slot = slot_config();

        sdmmc_card_t* detected = nullptr;
        const esp_err_t err = esp_vfs_fat_sdmmc_mount(
            kMountPoint, &host, &slot, &cfg, &detected);
        if (err != ESP_OK) {
            card = nullptr;
            mounted = false;
            return err;
        }

        card = detected;
        mounted = true;
        update_capacity();
        return ESP_OK;
    }

    void classify_mounted_volume() {
        update_capacity();

        if (path_exists(kMarker)) {
            set_status(SdVolumeState::Ready, "DEOS SD volume ready");
            return;
        }

        set_status(
            SdVolumeState::Foreign,
            "Readable card detected; DEOS has not initialized it");
    }

    esp_err_t probe() {
        set_status(SdVolumeState::Busy, "Checking SD card", "probe");

        const esp_err_t err = mount(false);
        if (err == ESP_OK) {
            classify_mounted_volume();
            initialized = true;
            return ESP_OK;
        }

        // ESP_FAIL means SDMMC card initialization succeeded but FAT could not
        // be mounted (unsupported/invalid filesystem or partition layout).
        if (err == ESP_FAIL) {
            set_status(
                SdVolumeState::NeedsFormat,
                "Card detected but its filesystem/layout is not supported");
            initialized = true;
            return ESP_OK;
        }

        if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_NOT_FOUND ||
            err == ESP_ERR_INVALID_RESPONSE) {
            set_status(SdVolumeState::Absent, "No usable SD card detected");
            initialized = true;
            return ESP_OK;
        }

        set_status(
            SdVolumeState::Error,
            std::string("SD probe failed: ") + esp_err_to_name(err));
        initialized = true;
        return ESP_OK;
    }

    esp_err_t create_deos_layout() {
        if (!mounted || card == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }

        for (const char* path : kDirectories) {
            ESP_RETURN_ON_ERROR(
                ensure_directory(path),
                kTag,
                "failed creating DEOS directory layout");
        }

        FILE* marker = std::fopen(kMarker, "w");
        if (marker == nullptr) {
            ESP_LOGE(kTag, "open marker failed: %s", std::strerror(errno));
            return ESP_FAIL;
        }

        const int write_result = std::fputs(
            "DEOS_VOLUME=1\n"
            "schema=1\n"
            "filesystem=fat\n"
            "portable=true\n",
            marker);
        const int flush_result = std::fflush(marker);
        const int close_result = std::fclose(marker);
        if (write_result < 0 || flush_result != 0 || close_result != 0) {
            ESP_LOGE(kTag, "writing volume marker failed: %s", std::strerror(errno));
            return ESP_FAIL;
        }

        update_capacity();
        set_status(SdVolumeState::Ready, "DEOS directory layout initialized");
        return ESP_OK;
    }

    esp_err_t initialize_for_deos() {
        if (!mounted) {
            const esp_err_t err = mount(false);
            if (err != ESP_OK) {
                return err;
            }
        }
        return create_deos_layout();
    }

    esp_err_t wipe_partition_metadata(sdmmc_card_t* target) {
        if (target == nullptr || target->csd.sector_size <= 0 || target->csd.capacity <= 0) {
            return ESP_ERR_INVALID_STATE;
        }

        const size_t sector_size = static_cast<size_t>(target->csd.sector_size);
        const size_t sector_count = static_cast<size_t>(target->csd.capacity);
        auto* zero = static_cast<uint8_t*>(
            heap_caps_calloc(1, sector_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        if (zero == nullptr) {
            return ESP_ERR_NO_MEM;
        }

        // Clear both the primary partition metadata area and the tail where a
        // previous GPT backup header/table can survive an MBR-only repartition.
        const size_t head_count = std::min<size_t>(34, sector_count);
        for (size_t sector = 0; sector < head_count; ++sector) {
            const esp_err_t err = sdmmc_write_sectors(target, zero, sector, 1);
            if (err != ESP_OK) {
                heap_caps_free(zero);
                ESP_LOGE(kTag, "failed clearing SD metadata sector %u: %s",
                         static_cast<unsigned>(sector), esp_err_to_name(err));
                return err;
            }
        }

        if (sector_count > 34) {
            const size_t tail_count = std::min<size_t>(33, sector_count - head_count);
            const size_t tail_start = sector_count - tail_count;
            for (size_t sector = tail_start; sector < sector_count; ++sector) {
                const esp_err_t err = sdmmc_write_sectors(target, zero, sector, 1);
                if (err != ESP_OK) {
                    heap_caps_free(zero);
                    ESP_LOGE(kTag, "failed clearing SD tail metadata sector %u: %s",
                             static_cast<unsigned>(sector), esp_err_to_name(err));
                    return err;
                }
            }
        }

        heap_caps_free(zero);
        ESP_LOGI(kTag, "Cleared stale MBR/GPT metadata areas");
        return ESP_OK;
    }

    esp_err_t wipe_unmounted_card_metadata() {
        sdmmc_host_t host = host_config();
        sdmmc_slot_config_t slot = slot_config();
        sdmmc_card_t temporary_card{};
        bool host_initialized = false;
        bool card_initialized = false;

        esp_err_t err = host.init();
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "SDMMC host init for cleanup failed: %s", esp_err_to_name(err));
            return err;
        }
        host_initialized = true;

        err = sdmmc_host_init_slot(host.slot, &slot);
        if (err == ESP_OK) {
            err = sdmmc_card_init(&host, &temporary_card);
            card_initialized = (err == ESP_OK);
        }

        if (err == ESP_OK) {
            err = wipe_partition_metadata(&temporary_card);
        }

        // ESP-IDF 6.1 does not expose sdmmc_card_deinit().
        // SDMMC_HOST_DEFAULT() also does not set SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF,
        // so this temporary card owns no separate buffer that needs releasing.
        // Deinitializing the host/slot below is the matching cleanup path.
        (void)card_initialized;
        if (host_initialized) {
            const esp_err_t deinit_err =
                (host.flags & SDMMC_HOST_FLAG_DEINIT_ARG)
                    ? host.deinit_p(host.slot)
                    : host.deinit();
            if (err == ESP_OK && deinit_err != ESP_OK) {
                err = deinit_err;
            }
        }

        if (err != ESP_OK) {
            ESP_LOGE(kTag, "SD metadata cleanup failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    esp_err_t repartition_mounted_card() {
        if (!mounted || card == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }

        const BYTE pdrv = ff_diskio_get_pdrv_card(card);
        if (pdrv == 0xFF) {
            ESP_LOGE(kTag, "SD physical drive is not registered");
            return ESP_ERR_INVALID_STATE;
        }

        char drive[3] = {
            static_cast<char>('0' + pdrv),
            ':',
            '\0',
        };
        FRESULT result = f_mount(nullptr, drive, 0);
        if (result != FR_OK) {
            ESP_LOGE(kTag, "FAT unmount before repartition failed: %d", result);
            return ESP_FAIL;
        }

        ESP_RETURN_ON_ERROR(
            wipe_partition_metadata(card),
            kTag,
            "failed clearing stale partition metadata");

        void* work = std::malloc(FF_MAX_SS);
        if (work == nullptr) {
            return ESP_ERR_NO_MEM;
        }

        const LBA_t partitions[4] = {100, 0, 0, 0};
        result = f_fdisk(pdrv, partitions, work);
        std::free(work);

        if (result != FR_OK) {
            ESP_LOGE(kTag, "f_fdisk failed: %d", result);
            return ESP_FAIL;
        }

        ESP_LOGI(kTag, "SD partition table replaced with one 100%% DEOS partition");
        return ESP_OK;
    }

    esp_err_t format_for_deos() {
        esp_err_t err = ESP_OK;

        if (mounted && card != nullptr) {
            ESP_LOGW(kTag, "Repartitioning and formatting mounted SD card by explicit user request");
            ESP_RETURN_ON_ERROR(
                repartition_mounted_card(),
                kTag,
                "SD repartition failed");

            // repartition_mounted_card() intentionally unmounts FatFs before
            // rewriting the partition table. esp_vfs_fat_sdcard_format()
            // requires a mounted FAT volume, so do not call it here. Release
            // the old VFS/card registration and remount with explicit
            // format_if_mount_failed instead. That creates FAT on the new
            // canonical full-card partition using public ESP-IDF APIs.
            sdmmc_card_t* old_card = card;
            const esp_err_t unmount_err =
                esp_vfs_fat_sdcard_unmount(kMountPoint, old_card);
            if (unmount_err != ESP_OK) {
                ESP_LOGE(
                    kTag,
                    "SD VFS cleanup after repartition failed: %s",
                    esp_err_to_name(unmount_err));
                return unmount_err;
            }

            mounted = false;
            card = nullptr;

            err = mount(true);
            if (err != ESP_OK) {
                return err;
            }
        } else {
            // This path is used for a card which SDMMC can initialize but FAT
            // cannot mount. Clean stale GPT/MBR metadata first, then let the
            // public ESP-IDF helper create a fresh single-partition FAT volume.
            ESP_LOGW(kTag, "Cleaning and formatting unsupported SD card by explicit user request");
            ESP_RETURN_ON_ERROR(
                wipe_unmounted_card_metadata(),
                kTag,
                "SD metadata cleanup failed");
            err = mount(true);
            if (err != ESP_OK) {
                return err;
            }
        }

        return create_deos_layout();
    }

    static void worker(void* context) {
        auto* self = static_cast<Impl*>(context);

        Operation current = Operation::None;
        self->lock();
        current = self->operation;
        self->unlock();

        esp_err_t err = ESP_ERR_INVALID_STATE;
        if (current == Operation::Probe) {
            self->set_status(
                SdVolumeState::Busy,
                "Checking SD card",
                "probe");
            err = self->probe();
        } else if (current == Operation::Initialize) {
            self->set_status(
                SdVolumeState::Busy,
                "Creating DEOS directories; existing files are preserved",
                "initialize");
            err = self->initialize_for_deos();
        } else if (current == Operation::Format) {
            self->set_status(
                SdVolumeState::Busy,
                "Formatting SD card and creating DEOS volume",
                "format");
            err = self->format_for_deos();
        }

        if (err != ESP_OK) {
            self->set_status(
                SdVolumeState::Error,
                std::string("Storage operation failed: ") + esp_err_to_name(err));
        }

        self->lock();
        self->operation = Operation::None;
        self->unlock();
        vTaskDelete(nullptr);
    }

    bool request(Operation requested) {
        lock();
        if (operation != Operation::None || status.state == SdVolumeState::Busy) {
            unlock();
            return false;
        }
        operation = requested;
        unlock();

        const char* task_name = requested == Operation::Format
                                    ? "deos-sd-format"
                                    : (requested == Operation::Probe
                                           ? "deos-sd-probe"
                                           : "deos-sd-init");
        const BaseType_t result = xTaskCreate(
            worker,
            task_name,
            kWorkerStack,
            this,
            kWorkerPriority,
            nullptr);
        if (result != pdPASS) {
            lock();
            operation = Operation::None;
            unlock();
            set_status(SdVolumeState::Error, "Could not start storage worker");
            return false;
        }
        return true;
    }
};

StorageController::StorageController(EntityRegistry& entities)
    : impl_(std::make_unique<Impl>(entities)) {}
StorageController::~StorageController() = default;

bool StorageController::supports(std::string_view kind) const {
    return kind == "Storage";
}

ResourceStatus StorageController::reconcile(const Resource&,
                                            const AppliedResource*) {
    if (!impl_->initialized) {
        const esp_err_t err = impl_->probe();
        if (err != ESP_OK) {
            return {
                Phase::Error,
                std::string("storage probe failed: ") + esp_err_to_name(err),
                {}
            };
        }
    }

    const SdVolumeSnapshot state = snapshot();
    return {
        Phase::Ready,
        state.message,
        {
            {"state", to_string(state.state)},
            {"mount", kMountPoint},
            {"policy", "never-auto-format"},
            {"marker", "DEOS/.volume"},
        }
    };
}

ResourceStatus StorageController::remove(const AppliedResource&) {
    return {
        Phase::Error,
        "hot storage removal is not implemented yet",
        {}
    };
}

SdVolumeSnapshot StorageController::snapshot() const {
    impl_->lock();
    SdVolumeSnapshot copy = impl_->status;
    impl_->unlock();
    return copy;
}

bool StorageController::request_rescan() {
    const SdVolumeSnapshot state = snapshot();
    if (state.state != SdVolumeState::Absent &&
        state.state != SdVolumeState::Error) {
        return false;
    }
    return impl_->request(Impl::Operation::Probe);
}

bool StorageController::request_initialize_for_deos() {
    const SdVolumeSnapshot state = snapshot();
    if (state.state != SdVolumeState::Foreign) {
        return false;
    }
    return impl_->request(Impl::Operation::Initialize);
}

bool StorageController::request_format_for_deos() {
    const SdVolumeSnapshot state = snapshot();
    if (state.state != SdVolumeState::Foreign &&
        state.state != SdVolumeState::NeedsFormat) {
        return false;
    }
    return impl_->request(Impl::Operation::Format);
}

}  // namespace deos::platform
