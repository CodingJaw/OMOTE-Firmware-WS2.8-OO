#if defined(OMOTE_CONFIG_V2_PREVIEW) && !defined(IS_SIMULATOR)
#include "config_v2_pairing_attempt_nvs.hpp"
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace config_v2_pairing_attempt_nvs {
namespace {
constexpr char kNamespace[] = "omotepair";
constexpr char kCounter[] = "attempts";

// Serialize read-modify-write across concurrent provisional sessions.
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

config_v2_pairing_attempt_budget::Reservation Store::reserve(std::uint32_t maximum) {
  using config_v2_pairing_attempt_budget::Reservation;
  ScopedLock lock;
  if (!lock.held() || !maximum) return Reservation::StorageError;
  Preferences nvs;
  if (!nvs.begin(kNamespace, false)) return Reservation::StorageError;
  const bool exists = nvs.isKey(kCounter);
  const std::uint32_t previous = nvs.getUInt(kCounter, UINT32_MAX);
  // Absent key starts at zero. Wrong-type/unreadable key fails shut.
  if (exists && previous == UINT32_MAX) {
    nvs.end();
    return Reservation::StorageError;
  }
  const std::uint32_t attempts = exists ? previous : 0u;
  if (attempts >= maximum) {
    nvs.end();
    return Reservation::Exhausted;
  }
  const std::uint32_t next = attempts + 1u;
  const bool written = nvs.putUInt(kCounter, next) == sizeof(std::uint32_t);
  const bool matches = written && nvs.getUInt(kCounter, 0) == next;
  nvs.end();
  return matches ? Reservation::Allowed : Reservation::StorageError;
}

bool Store::reset() {
  ScopedLock lock;
  if (!lock.held()) return false;
  Preferences nvs;
  if (!nvs.begin(kNamespace, false)) return false;
  const bool written = nvs.putUInt(kCounter, 0u) == sizeof(std::uint32_t);
  const bool matches = written && nvs.getUInt(kCounter, UINT32_MAX) == 0u;
  nvs.end();
  return matches;
}
} // namespace config_v2_pairing_attempt_nvs
#endif
