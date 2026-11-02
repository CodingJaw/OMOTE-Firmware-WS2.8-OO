#pragma once
// ESP32 backing store for pairing attempt limits, isolated from active config.
#include "config_v2_pairing_attempt_budget.hpp"
namespace config_v2_pairing_attempt_nvs {
class Store {
public:
  config_v2_pairing_attempt_budget::Reservation reserve(std::uint32_t maximum);
  bool reset();
};
} // namespace config_v2_pairing_attempt_nvs
