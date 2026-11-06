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

LockState Store::lockState(std::uint32_t maxWindows,
                           std::uint32_t maxMessages) {
  Guard guard;
  if (!guard.held() || maxWindows == 0 || maxMessages == 0)
    return LockState::StorageError;
  Preferences nvs;
  // New lab images may not have a namespace yet. Creating the empty
  // namespace must not set, remove or increment either budget key.
  if (!nvs.begin(kNamespace, false)) return LockState::StorageError;
  const bool windowsExist = nvs.isKey(kCounter);
  const bool messagesExist = nvs.isKey(kSecMessageCounter);
  const std::uint32_t windows = nvs.getUInt(kCounter, UINT32_MAX);
  const std::uint32_t messages = nvs.getUInt(kSecMessageCounter, UINT32_MAX);
  nvs.end();

  if ((windowsExist && windows == UINT32_MAX) ||
      (messagesExist && messages == UINT32_MAX) ||
      (windowsExist && windows > maxWindows) ||
      (messagesExist && messages > maxMessages))
    return LockState::StorageError;
  if ((windowsExist && windows >= maxWindows) ||
      (messagesExist && messages >= maxMessages))
    return LockState::Exhausted;
  return LockState::Ready;
}

bool Store::reset() {
  // NEVER expose this operation via a network, USB serial, timer or
  // automatic reboot path. The only caller is the two-confirmation,
  // cooldown-gated physical Settings touchscreen callback.
  Guard guard;
  if (!guard.held()) return false;
  Preferences nvs;
  if (!nvs.begin(kNamespace, false)) return false;

  // Both quota keys belong solely to this isolated lab. A failed second
  // write leaves an unusable/locked lab rather than opening an HTTP service;
  // do not attempt to clear all NVS or other namespaces.
  const bool windowsWritten =
      nvs.putUInt(kCounter, 0u) == sizeof(std::uint32_t);
  const bool windowsVerified =
      windowsWritten && nvs.getUInt(kCounter, UINT32_MAX) == 0u;
  const bool messagesWritten = windowsVerified &&
      nvs.putUInt(kSecMessageCounter, 0u) == sizeof(std::uint32_t);
  const bool messagesVerified = messagesWritten &&
      nvs.getUInt(kSecMessageCounter, UINT32_MAX) == 0u;
  nvs.end();
  return windowsVerified && messagesVerified;
}
} // namespace config_v2_sec2_lab_budget
#endif
