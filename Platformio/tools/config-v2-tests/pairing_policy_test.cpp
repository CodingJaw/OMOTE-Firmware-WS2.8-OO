// Pure local pairing interaction tests. NOT a cryptographic pairing test.
#include "../../lib/esp32HalImpl/wifiHandler/config_v2_pairing_policy.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

using namespace config_v2_pairing_policy;

static VerifiedPeer peer() {
  return {std::string(64, 'a'), std::string(32, 'b')};
}

int main() {
  Window pairing;
  VerifiedPeer out;
  // Leading zeros matter: six digits, not a variable-width integer.
  assert(pairing.begin(Entry::SettingsTouch, 100, [] { return 42u; }) == Status::Ok);
  assert(pairing.entry() == Entry::SettingsTouch);
  assert(pairing.codeForLocalScreen(101) == "000042");
  assert(pairing.begin(Entry::PhysicalKeyChord, 102, [] { return 3u; }) == Status::Busy);
  assert(pairing.submitVerifiedPeerCode("000042", {"not-a-fingerprint", std::string(32, 'b')}, 103) ==
         Status::Invalid);
  assert(pairing.failedAttempts() == 0);
  assert(pairing.submitVerifiedPeerCode("42", peer(), 104) == Status::WrongCode);
  assert(pairing.failedAttempts() == 1);
  assert(pairing.takeConfirmedPeer(out, 105) == Status::NotConfirmed);
  assert(pairing.confirmTouch(106) == Status::WrongPhase);
  assert(pairing.submitVerifiedPeerCode("000042", peer(), 107) == Status::Ok);
  assert(pairing.phase(107) == Phase::AwaitingTouch);
  assert(pairing.codeForLocalScreen(108).empty());
  assert(pairing.peerForLocalScreen(108)->publicKeyFingerprint == std::string(64, 'a'));
  assert(pairing.submitVerifiedPeerCode("000042", peer(), 109) == Status::WrongPhase);
  assert(pairing.confirmTouch(110) == Status::Ok);
  assert(pairing.confirmTouch(111) == Status::WrongPhase);
  assert(pairing.takeConfirmedPeer(out, 112) == Status::Ok);
  assert(out.publicKeyFingerprint == std::string(64, 'a'));
  assert(out.secureSessionId == std::string(32, 'b'));
  assert(pairing.phase(113) == Phase::Idle);
  assert(pairing.takeConfirmedPeer(out, 114) == Status::NotConfirmed);

  // Separate explicit entry path: a future actual keypad chord may call
  // begin() without needing to expose a network pairing-open endpoint.
  assert(pairing.begin(Entry::PhysicalKeyChord, 200, [] { return 123456u; }) == Status::Ok);
  assert(pairing.entry() == Entry::PhysicalKeyChord);
  assert(pairing.codeForLocalScreen(201) == "123456");
  pairing.cancel();
  assert(pairing.phase(201) == Phase::Idle);
  assert(pairing.codeForLocalScreen(202).empty());

  // Uniform generation rejects the short tail of the uint32_t range.
  unsigned draws = 0;
  assert(pairing.begin(Entry::SettingsTouch, 300, [&] {
    return draws++ == 0 ? 4294000000u : 999999u;
  }) == Status::Ok);
  assert(draws == 2);
  assert(pairing.codeForLocalScreen(301) == "999999");
  pairing.cancel();
  assert(pairing.begin(Entry::SettingsTouch, 310,
                       [] { return std::uint32_t{0xffffffffu}; }) ==
         Status::EntropyUnavailable);
  assert(pairing.phase(311) == Phase::Idle);

  // Five incorrect guesses fail closed. Additional guesses cannot succeed,
  // even with the correct code, until the cooldown has elapsed.
  assert(pairing.begin(Entry::SettingsTouch, 500, [] { return 555555u; }) == Status::Ok);
  for (unsigned i = 1; i < kMaximumWrongCodes; ++i)
    assert(pairing.submitVerifiedPeerCode("555554", peer(), 500 + i) == Status::WrongCode);
  assert(pairing.submitVerifiedPeerCode("555554", peer(), 510) ==
         Status::AttemptsExhausted);
  assert(pairing.lockedOut(510));
  assert(pairing.submitVerifiedPeerCode("555555", peer(), 511) == Status::Cooldown);
  pairing.cancel();
  assert(pairing.begin(Entry::SettingsTouch, 512, [] { return 5u; }) == Status::Cooldown);
  assert(!pairing.lockedOut(510 + kCooldownMs));
  assert(pairing.begin(Entry::SettingsTouch, 510 + kCooldownMs,
                       [] { return 123u; }) == Status::Ok);
  assert(pairing.codeForLocalScreen(510 + kCooldownMs) == "000123");
  pairing.cancel();

  // Time expiry kills the code and disallows later confirmation.
  assert(pairing.begin(Entry::SettingsTouch, 1000000, [] { return 123456u; }) == Status::Ok);
  assert(pairing.codeForLocalScreen(1000000 + kWindowMs - 1) == "123456");
  assert(pairing.codeForLocalScreen(1000000 + kWindowMs).empty());
  assert(pairing.phase(1000000 + kWindowMs) == Phase::Idle);

  // A peer code is insufficient without a separate local confirmation.
  assert(pairing.begin(Entry::SettingsTouch, 2000000, [] { return 123456u; }) == Status::Ok);
  assert(pairing.submitVerifiedPeerCode("123456", peer(), 2000001) == Status::Ok);
  assert(pairing.takeConfirmedPeer(out, 2000002) == Status::NotConfirmed);
  assert(pairing.confirmTouch(2000001 + kTouchWindowMs) == Status::Expired);
  assert(pairing.takeConfirmedPeer(out, 2000001 + kTouchWindowMs + 1) ==
         Status::NotConfirmed);

  // Wraparound-safe expiration and local button-chord support.
  constexpr std::uint32_t nearWrap = 0xfffffff0u;
  assert(pairing.begin(Entry::PhysicalKeyChord, nearWrap, [] { return 101010u; }) == Status::Ok);
  assert(pairing.codeForLocalScreen(11) == "101010");
  assert(pairing.submitVerifiedPeerCode("101010", peer(), 12) == Status::Ok);
  assert(pairing.confirmTouch(13) == Status::Ok);
  assert(pairing.takeConfirmedPeer(out, 14) == Status::Ok);
  assert(pairing.phase(15) == Phase::Idle);

  std::cout << "config v2 local pairing policy: PASS\n";
}
