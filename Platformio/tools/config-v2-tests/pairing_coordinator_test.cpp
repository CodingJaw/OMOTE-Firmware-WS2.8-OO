// Host-only orchestration tests. Verifier is a MOCK; no PAKE exists here.
#include "../../lib/esp32HalImpl/wifiHandler/config_v2_pairing_coordinator.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

using config_v2_pairing_coordinator::Coordinator;
using config_v2_pairing_coordinator::Result;
using config_v2_pairing_policy::Entry;
using config_v2_pairing_policy::Status;
using config_v2_pairing_attempt_budget::Reservation;

struct MockVerifier {
  using Handle = std::string;
  bool authenticated = true;
  std::string identity = std::string(64, 'a');
  std::string session = std::string(32, 'f');
  bool verifySession(const Handle &handle,
                     config_v2_pairing_policy::VerifiedPeer &out) {
    if (!authenticated || handle != "opaque-pake-session") return false;
    out = {identity, session};
    return true;
  }
};
struct MemoryNvs {
  unsigned count = 0;
  bool fail = false;
  Reservation reserve(unsigned limit) {
    if (fail) return Reservation::StorageError;
    if (count >= limit) return Reservation::Exhausted;
    ++count;
    return Reservation::Allowed;
  }
  bool reset() {
    if (fail) return false;
    count = 0;
    return true;
  }
};
struct MockTrust {
  bool fail = false;
  unsigned writes = 0;
  std::string current = std::string(64, 'b');
  bool replaceAfterAuthenticatedPhysicalApproval(const std::string &fingerprint) {
    if (fail) return false;
    current = fingerprint; ++writes;
    return true;
  }
};

using Attempts = config_v2_pairing_attempt_budget::Gate<MemoryNvs>;
using Pairing = Coordinator<MockVerifier, Attempts, MockTrust>;
const std::string handle = "opaque-pake-session";
static auto entropy = [] { return 42u; };

int main() {
  MemoryNvs nvs;
  MockVerifier verifier;
  MockTrust trust;
  Attempts attempts(nvs, 0);
  Pairing pairing(verifier, attempts, trust);

  assert(pairing.begin(Entry::SettingsTouch, 100, entropy) == Status::Ok);
  assert(pairing.codeForLocalScreen(101) == "000042");
  assert(pairing.approveFromLocalTouch(102) == Result::NotReady);
  assert(trust.writes == 0);
  assert(pairing.submitFromAuthenticatedChannel("unverified", "000042", 103) ==
         Result::TransportUnverified);
  assert(nvs.count == 0);
  assert(pairing.submitFromAuthenticatedChannel(handle, "000043", 104) ==
         Result::WrongCode);
  assert(nvs.count == 1 && trust.writes == 0);
  assert(pairing.submitFromAuthenticatedChannel(handle, "000042", 105) ==
         Result::TouchRequired);
  assert(pairing.codeForLocalScreen(106).empty());
  assert(pairing.pendingPeerFingerprint(106) == verifier.identity);
  assert(pairing.submitFromAuthenticatedChannel(handle, "000042", 106) ==
         Result::NotReady);
  assert(pairing.approveFromLocalTouch(107) == Result::Ok);
  assert(trust.current == verifier.identity && trust.writes == 1);
  assert(nvs.count == 0);
  assert(pairing.approveFromLocalTouch(108) == Result::NotReady);
  assert(pairing.pendingPeerFingerprint(109).empty());

  // Explicit rejection must keep old trust and invalidate the code/session.
  assert(pairing.begin(Entry::SettingsTouch, 200, entropy) == Status::Ok);
  assert(pairing.submitFromAuthenticatedChannel(handle, "000042", 201) ==
         Result::TouchRequired);
  assert(pairing.rejectFromLocalTouch() == Result::Rejected);
  assert(pairing.approveFromLocalTouch(202) == Result::NotReady);
  assert(trust.writes == 1);

  // Peer revocation / session replacement between code and touchscreen:
  // fail closed without changing the trusted identity.
  assert(pairing.begin(Entry::SettingsTouch, 300, entropy) == Status::Ok);
  assert(pairing.submitFromAuthenticatedChannel(handle, "000042", 301) ==
         Result::TouchRequired);
  verifier.session = std::string(32, 'e');
  assert(pairing.approveFromLocalTouch(302) == Result::PeerChanged);
  assert(trust.writes == 1);
  verifier.session = std::string(32, 'f');

  // Failed NVS trust replacement must not report pairing success.
  assert(pairing.begin(Entry::SettingsTouch, 400, entropy) == Status::Ok);
  assert(pairing.submitFromAuthenticatedChannel(handle, "000042", 401) ==
         Result::TouchRequired);
  trust.fail = true;
  assert(pairing.approveFromLocalTouch(402) == Result::StorageError);
  assert(trust.writes == 1);
  trust.fail = false;

  // If trust persists but the attempt counter cannot clear, report degraded
  // state rather than falsely claiming an unlocked pairing.
  assert(pairing.begin(Entry::SettingsTouch, 500, entropy) == Status::Ok);
  assert(pairing.submitFromAuthenticatedChannel(handle, "000042", 501) ==
         Result::TouchRequired);
  nvs.fail = true;
  assert(pairing.approveFromLocalTouch(502) == Result::PersistedButLocked);
  assert(trust.writes == 2);
  nvs.fail = false;

  // Persistent attempts survive destruction/recreation of coordinator.
  pairing.cancel();
  MemoryNvs separate;
  MockTrust replacement;
  Attempts firstBudget(separate, 0);
  Pairing first(verifier, firstBudget, replacement);
  assert(first.begin(Entry::SettingsTouch, 600, entropy) == Status::Ok);
  for (unsigned i = 0; i < 5; ++i) {
    const auto s = first.submitFromAuthenticatedChannel(handle, "bad", 601 + i);
    assert(s == Result::WrongCode);
  }
  assert(separate.count == 5);
  Attempts rebootBudget(separate, 0);
  Pairing rebooted(verifier, rebootBudget, replacement);
  assert(rebooted.begin(Entry::SettingsTouch, 700, entropy) == Status::Ok);
  assert(rebooted.submitFromAuthenticatedChannel(handle, "000042", 701) ==
         Result::RateLimited);
  assert(replacement.writes == 0);

  // Expired verification cannot be approved after the local 60-second gate.
  MemoryNvs fresh;
  Attempts freshBudget(fresh, 0);
  Pairing expires(verifier, freshBudget, replacement);
  assert(expires.begin(Entry::SettingsTouch, 100000, entropy) == Status::Ok);
  assert(expires.submitFromAuthenticatedChannel(handle, "000042", 100001) ==
         Result::TouchRequired);
  assert(expires.approveFromLocalTouch(100001 +
          config_v2_pairing_policy::kTouchWindowMs) != Result::Ok);
  assert(replacement.writes == 0);

  std::cout << "config v2 pairing coordinator (mock session): PASS\n";
}
