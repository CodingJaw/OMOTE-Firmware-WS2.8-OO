// Host-only persistence and lockout policy tests, not an actual PAKE test.
#include "../../lib/esp32HalImpl/wifiHandler/config_v2_pairing_attempt_budget.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace config_v2_pairing_attempt_budget;

struct FakePersistentStore {
  std::uint32_t attempts = 0;
  bool ioError = false;
  unsigned writes = 0;
  Reservation reserve(std::uint32_t maximum) {
    if (ioError) return Reservation::StorageError;
    if (attempts >= maximum) return Reservation::Exhausted;
    ++attempts; ++writes; return Reservation::Allowed;
  }
  bool reset() {
    if (ioError) return false;
    attempts = 0; ++writes; return true;
  }
};

int main() {
  FakePersistentStore store;
  Gate<FakePersistentStore> first(store, 0);
  Gate<FakePersistentStore> concurrent(store, 0);
  assert(first.rearmAfterLocalTouch(kRearmDelayMs) == Status::NotLocked);
  for (unsigned i = 0; i < 4; ++i)
    assert(first.reserve(i * 10) == Status::Allowed);
  assert(concurrent.reserve(40) == Status::Allowed);
  assert(store.attempts == 5 && store.writes == 5);
  assert(first.reserve(50) == Status::Locked);
  assert(concurrent.reserve(51) == Status::Locked);
  assert(store.writes == 5);
  assert(first.rearmAfterLocalTouch(50 + kRearmDelayMs - 1) == Status::CoolingDown);
  assert(first.rearmAfterLocalTouch(50 + kRearmDelayMs) == Status::Reset);
  assert(store.attempts == 0);

  // Reboot cannot reset the persistent budget or accelerate local rearm.
  for (unsigned i = 0; i < 5; ++i)
    assert(first.reserve(200000 + i) == Status::Allowed);
  Gate<FakePersistentStore> afterReboot(store, 0);
  assert(afterReboot.reserve(1) == Status::Locked);
  assert(afterReboot.rearmAfterLocalTouch(kRearmDelayMs - 1) == Status::CoolingDown);
  assert(afterReboot.rearmAfterLocalTouch(kRearmDelayMs + 1) == Status::Reset);
  assert(afterReboot.reserve(kRearmDelayMs + 2) == Status::Allowed);

  // Never count an attempt as allowed if NVS reads/writes cannot be verified.
  store.ioError = true;
  assert(first.reserve(450000) == Status::StorageError);
  assert(first.clearAfterDurableApprovedPairing() == Status::StorageError);
  assert(store.attempts == 1);
  store.ioError = false;
  assert(first.clearAfterDurableApprovedPairing() == Status::Reset);
  assert(store.attempts == 0);

  // Millisecond counter wraparound is safe for the 120-second interval.
  store.attempts = 5;
  Gate<FakePersistentStore> wrapping(store, 0xfffffff0u);
  assert(wrapping.reserve(0xfffffff4u) == Status::Locked);
  assert(wrapping.rearmAfterLocalTouch(2) == Status::CoolingDown);
  assert(wrapping.rearmAfterLocalTouch(
    static_cast<std::uint32_t>(0xfffffff4u + kRearmDelayMs)) == Status::Reset);
  std::cout << "config v2 persisted pairing attempt budget: PASS\n";
}
