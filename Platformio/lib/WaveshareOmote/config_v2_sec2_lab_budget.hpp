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
class Store {
public:
  config_v2_pairing_attempt_budget::Reservation reserve(std::uint32_t maximum);
  // Reserved solely for a future explicit LOCAL physical rearm operation.
  // No application, network or timed call invokes reset().
  bool reset();
  // Reserve NVS counter before EVERY Security2 protocol message (including
  // initial and proof frames). Conservatively counts a full handshake twice.
  // The sixth message is refused even if the board rebooted.
  config_v2_pairing_attempt_budget::Reservation reserveSecurityMessage(
      std::uint32_t maximum);
};
} // namespace config_v2_sec2_lab_budget
#endif
