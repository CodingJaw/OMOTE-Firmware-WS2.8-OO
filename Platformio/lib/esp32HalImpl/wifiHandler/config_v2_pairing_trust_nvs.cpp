#if defined(OMOTE_CONFIG_V2_PREVIEW) && !defined(IS_SIMULATOR)
#include "config_v2_pairing_trust_nvs.hpp"
#include "config_v2_pairing_trust_record.hpp"
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace config_v2_pairing_trust_nvs {
namespace {
constexpr char kNamespace[] = "omotepair";
constexpr char kPeerKey[] = "haPeer";
SemaphoreHandle_t mutex() {
  static SemaphoreHandle_t handle = xSemaphoreCreateMutex();
  return handle;
}
class ScopedLock {
public:
  ScopedLock() : handle_(mutex()) {
    held_ = handle_ && xSemaphoreTake(handle_, pdMS_TO_TICKS(200)) == pdTRUE;
  }
  ~ScopedLock() { if (held_) xSemaphoreGive(handle_); }
  bool held() const { return held_; }
private:
  SemaphoreHandle_t handle_ = nullptr;
  bool held_ = false;
};
} // namespace

ReadStatus Store::read(std::string &fingerprint) {
  fingerprint.clear();
  ScopedLock lock;
  if (!lock.held()) return ReadStatus::IoError;
  Preferences nvs;
  if (!nvs.begin(kNamespace, false)) return ReadStatus::IoError;
  if (!nvs.isKey(kPeerKey)) { nvs.end(); return ReadStatus::Unpaired; }
  const String value = nvs.getString(kPeerKey, "");
  nvs.end();
  if (!config_v2_pairing_trust_record::decode(
          std::string_view(value.c_str(), value.length()), fingerprint))
    return ReadStatus::Corrupt;
  return ReadStatus::Paired;
}

bool Store::replaceAfterAuthenticatedPhysicalApproval(const std::string &fingerprint) {
  std::string encoded;
  if (!config_v2_pairing_trust_record::encode(fingerprint, encoded)) return false;
  ScopedLock lock;
  if (!lock.held()) return false;
  Preferences nvs;
  if (!nvs.begin(kNamespace, false)) return false;
  // One atomic NVS key update, no deletion of old trust or any device config.
  const bool wrote = nvs.putString(kPeerKey, encoded.c_str()) == encoded.size();
  const String saved = nvs.getString(kPeerKey, "");
  const bool verified = wrote && saved.length() == encoded.size() &&
      std::string_view(saved.c_str(), saved.length()) == encoded;
  nvs.end();
  return verified;
}
} // namespace config_v2_pairing_trust_nvs
#endif
