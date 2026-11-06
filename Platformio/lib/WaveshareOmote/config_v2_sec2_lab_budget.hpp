#pragma once
// LAB-ONLY rate limit for locally opened encrypted interoperability windows.
// NEVER use the production omotepair/attempts NVS namespace here.
// This counts physical window openings, NOT individual SRP candidate guesses;
// it is not an adequate production brute-force limiter.
#if defined(OMOTE_WAVESHARE28V2) && \
    defined(OMOTE_CONFIG_V2_PREVIEW) && \
    defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
#include "config_v2_pairing_attempt_budget.hpp"
#include <cstdint>
namespace config_v2_sec2_lab_budget {
// Read-only diagnosis of the two independent LAB quotas. An unreadable,
// malformed or out-of-range counter is a storage error, not permission to
// reset anything.
enum class LockState { Ready, Exhausted, StorageError };

class Store {
public:
  config_v2_pairing_attempt_budget::Reservation reserve(std::uint32_t maximum);
  // Inspect both flash-persistent LAB counters without reserving attempts.
  LockState lockState(std::uint32_t maxWindows, std::uint32_t maxMessages);
  // RESTRICTED to an explicit two-touch local touchscreen rearm with NO
  // cooldown. Resets BOTH lab counters, never
  // production omotepair data, configuration files or lab device identity.
  // No network/serial/boot/timer code is allowed to call reset().
  bool reset();
  // Reserve NVS counter before EVERY Security2 protocol message (including
  // initial and proof frames). Conservatively counts a full handshake twice.
  // The sixth message is refused even if the board rebooted.
  config_v2_pairing_attempt_budget::Reservation reserveSecurityMessage(
      std::uint32_t maximum);
};
} // namespace config_v2_sec2_lab_budget
#endif
