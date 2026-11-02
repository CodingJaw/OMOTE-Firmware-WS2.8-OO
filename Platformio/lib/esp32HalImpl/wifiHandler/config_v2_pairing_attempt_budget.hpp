#pragma once
// Persistent pairing-attempt budget; not a pairing protocol or server.
// Store::reserve(max) must ATOMICALLY read, increment, persist and read back
// the count BEFORE each candidate PIN is examined. Errors fail closed.
// Counter resets are privileged: only trusted physical rearm after cooldown,
// or after encrypted peer proof + touchscreen approval + durable trust commit.
#include <cstdint>

namespace config_v2_pairing_attempt_budget {
constexpr std::uint32_t kMaximumAttempts = 5;
constexpr std::uint32_t kRearmDelayMs = 120000;
enum class Reservation { Allowed, Exhausted, StorageError };
enum class Status { Allowed, Locked, NotLocked, CoolingDown, StorageError, Reset };

template <typename Store> class Gate {
public:
  Gate(Store &store, std::uint32_t bootUptimeMs)
      : store_(store), bootUptimeMs_(bootUptimeMs) {}

  // Exactly one atomic reservation per candidate code, even if malformed.
  // The fifth attempt is allowed; the sixth is rejected.
  Status reserve(std::uint32_t now) {
    const Reservation outcome = store_.reserve(kMaximumAttempts);
    if (outcome == Reservation::Allowed) return Status::Allowed;
    if (outcome == Reservation::StorageError) return Status::StorageError;
    if (!exhaustionSeen_) {
      exhaustionSeen_ = true;
      exhaustionAt_ = now;
    }
    return Status::Locked;
  }

  // MUST be called exclusively by a real local touchscreen gesture; never
  // expose through HTTP, MQTT, WebSocket or BLE. A reboot retains the NVS
  // attempts and restarts the minimum rearm delay.
  Status rearmAfterLocalTouch(std::uint32_t now) {
    if (!exhaustionSeen_) return Status::NotLocked;
    const std::uint32_t since = exhaustionSeen_ ? exhaustionAt_ : bootUptimeMs_;
    if (static_cast<std::uint32_t>(now - since) < kRearmDelayMs)
      return Status::CoolingDown;
    if (!store_.reset()) return Status::StorageError;
    exhaustionSeen_ = false;
    return Status::Reset;
  }

  // Only AFTER verified cryptographic peer proof, one-shot local approval,
  // and atomic read-back-verified NVS trust-store update. No caller yet.
  Status clearAfterDurableApprovedPairing() {
    if (!store_.reset()) return Status::StorageError;
    exhaustionSeen_ = false;
    return Status::Reset;
  }
private:
  Store &store_;
  std::uint32_t bootUptimeMs_ = 0;
  std::uint32_t exhaustionAt_ = 0;
  bool exhaustionSeen_ = false;
};
} // namespace config_v2_pairing_attempt_budget
