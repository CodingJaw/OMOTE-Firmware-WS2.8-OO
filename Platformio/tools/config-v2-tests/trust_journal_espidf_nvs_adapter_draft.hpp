// DISCONNECTED EXPERIMENTAL ESP-IDF NVS BACKEND — NOT IN PLATFORMIO BUILD.
//
// The host-tested storage core is trust_journal_slots_draft.hpp. This
// translation unit is intentionally not referenced from any firmware
// source file. It contains NO nvs_open call or automatic initialization.
// Do not instantiate it on the Waveshare without explicit review of
// partitions, key storage, corruption recovery and power-cut testing.
//
// WARNING: NVS blobs and a CRC32 are NOT authenticated, encrypted,
// anti-rollback, or protected against attackers with flash access.
// No eFuse, flash encryption, secure boot or pairing state is changed.
#pragma once
#include "trust_journal_slots_draft.hpp"
#include "esp_err.h"
#include "nvs.h"
#include <string>
#include <vector>

namespace omote_trust_lab {
class UnlinkedNvsBackend final : public Backend {
public:
    // Caller owns and closes the NVS handle; must be READWRITE on a
    // separately approved namespace, not any existing firmware settings.
    explicit UnlinkedNvsBackend(nvs_handle_t handle) : handle_(handle) {}
    ReadResult read(unsigned slot, std::string& bytes) const override {
        if (slot > 1) return ReadResult::Error;
        const char* key = slot == 0 ? "omj_draft0" : "omj_draft1";
        size_t size = 0;
        esp_err_t status = nvs_get_blob(handle_, key, nullptr, &size);
        if (status == ESP_ERR_NVS_NOT_FOUND) return ReadResult::Missing;
        if (status != ESP_OK || size == 0 || size > 512)
            return ReadResult::Error;
        std::vector<char> value(size);
        status = nvs_get_blob(handle_, key, value.data(), &size);
        if (status != ESP_OK || size != value.size()) return ReadResult::Error;
        bytes.assign(value.data(), value.size());
        return ReadResult::Present;
    }
    bool write(unsigned slot, const std::string& bytes) override {
        if (slot > 1 || bytes.empty() || bytes.size() > 512)
            return false;
        const char* key = slot == 0 ? "omj_draft0" : "omj_draft1";
        if (nvs_set_blob(handle_, key, bytes.data(), bytes.size()) != ESP_OK)
            return false;
        // esp-idf nvs_commit() is required before assuming a write persisted.
        if (nvs_commit(handle_) != ESP_OK) return false;
        std::string confirmation;
        return read(slot, confirmation) == ReadResult::Present &&
               confirmation == bytes;
    }
private:
    nvs_handle_t handle_;
};
}  // namespace omote_trust_lab
