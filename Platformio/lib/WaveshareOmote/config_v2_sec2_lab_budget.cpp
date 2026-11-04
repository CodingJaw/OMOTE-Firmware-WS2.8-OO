// Standalone NVS budget for physically opened Security2 LAB windows only.
// Omote production pairing attempt counters and HA trust are untouched.
#if defined(OMOTE_WAVESHARE28V2) && \
    defined(OMOTE_CONFIG_V2_PREVIEW) && \
    defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
#include "config_v2_sec2_lab_budget.hpp"
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdint>

namespace config_v2_sec2_lab_budget {
namespace {
constexpr char kNamespace[] = "omsec2lab";
constexpr char kCounter[] = "windows";
constexpr char kSecMessageCounter[] = "secmsgs";
SemaphoreHandle_t budgetMutex() {
  static SemaphoreHandle_t sem = xSemaphoreCreateMutex();
  return sem;
}
class Guard {
public:
  Guard() : sem_(budgetMutex()) {
    held_ = sem_ && xSemaphoreTake(sem_, pdMS_TO_TICKS(200)) == pdTRUE;
  }
  ~Guard() { if (held_) xSemaphoreGive(sem_); }
  bool held() const { return held_; }
private:
  SemaphoreHandle_t sem_ = nullptr;
  bool held_ = false;
};
} // namespace

config_v2_pairing_attempt_budget::Reservation
reserveKey(const char *key, std::uint32_t maximum) {
  using config_v2_pairing_attempt_budget::Reservation;
  Guard guard;
  if (!guard.held() || maximum == 0) return Reservation::StorageError;
  Preferences nvs;
  if (!nvs.begin(kNamespace, false)) return Reservation::StorageError;
  const bool exists = nvs.isKey(key);
  const std::uint32_t previous = nvs.getUInt(key, UINT32_MAX);
  if (exists && previous == UINT32_MAX) {
    nvs.end();
    return Reservation::StorageError;
  }
  const std::uint32_t used = exists ? previous : 0u;
  if (used >= maximum) {
    nvs.end();
    return Reservation::Exhausted;
  }
  const auto next = used + 1u;
  const bool written = nvs.putUInt(key, next) == sizeof(next);
  const bool verified = written && nvs.getUInt(key, UINT32_MAX) == next;
  nvs.end();
  return verified ? Reservation::Allowed : Reservation::StorageError;
}


config_v2_pairing_attempt_budget::Reservation
Store::reserve(std::uint32_t maximum) {
  return reserveKey(kCounter, maximum);
}

config_v2_pairing_attempt_budget::Reservation
Store::reserveSecurityMessage(std::uint32_t maximum) {
  return reserveKey(kSecMessageCounter, maximum);
}

bool Store::reset() {
  Guard guard;
  if (!guard.held()) return false;
  Preferences nvs;
  if (!nvs.begin(kNamespace, false)) return false;
  const bool written = nvs.putUInt(kCounter, 0u) == sizeof(std::uint32_t);
  const bool verified = written && nvs.getUInt(kCounter, UINT32_MAX) == 0u;
  nvs.end();
  return verified;
}
} // namespace config_v2_sec2_lab_budget
#endif
