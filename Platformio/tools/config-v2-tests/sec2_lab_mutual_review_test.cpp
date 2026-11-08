// Pure host-side mutual key proof/session review tests.
// Inputs simulate VERIFIED crypto events; no firmware trust is granted.
#include "../../lib/esp32HalImpl/wifiHandler/config_v2_sec2_lab_mutual_review.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
namespace review = config_v2_sec2_lab_mutual_review;

const std::string ha(64, 'a');
const std::string device(64, 'b');
constexpr std::uint32_t session = 0x12345678u;

int main() {
  review::Review state;
  assert(state.snapshot(100).state == review::State::Closed);
  assert(state.observeVerifiedHa(session, ha, 100) == review::Result::NotReady);
  assert(state.acknowledgeFromLocalTouch(100) == review::Result::NotReady);

  // A real local action must start collection; the two key proofs can arrive
  // in either order but MUST have exactly matching authenticated sessions.
  state.beginFromLocalTouch(200);
  assert(state.observeVerifiedHa(session, ha, 201) == review::Result::Ok);
  assert(state.acknowledgeFromLocalTouch(202) == review::Result::NotReady);
  assert(state.snapshot(203).haFingerprint.empty());
  assert(state.observeVerifiedDevice(session, device, 204) ==
         review::Result::TouchRequired);
  auto pending = state.snapshot(205);
  assert(pending.state == review::State::AwaitingTouch);
  assert(pending.haFingerprint == ha && pending.deviceFingerprint == device);
  assert(pending.verifiedSession == session);
  assert(!pending.trustGranted && !pending.deploymentEnabled);
  assert(state.acknowledgeFromLocalTouch(206) ==
         review::Result::LocalReviewOnly);
  assert(state.snapshot(207).state == review::State::Reviewed);
  assert(!state.snapshot(207).trustGranted);
  assert(state.acknowledgeFromLocalTouch(208) == review::Result::NotReady);
  assert(state.observeVerifiedHa(session, ha, 209) == review::Result::NotReady);
  state.close();
  assert(state.snapshot(210).state == review::State::Closed);
  assert(state.snapshot(210).haFingerprint.empty());

  state.beginFromLocalTouch(1000);
  assert(state.observeVerifiedDevice(session, device, 1001) == review::Result::Ok);
  assert(state.observeVerifiedHa(session, ha, 1002) ==
         review::Result::TouchRequired);
  assert(state.snapshot(1003).state == review::State::AwaitingTouch);

  // Different encrypted session IDs cannot be merged into a candidate.
  state.beginFromLocalTouch(2000);
  assert(state.observeVerifiedHa(session, ha, 2001) == review::Result::Ok);
  assert(state.observeVerifiedDevice(session + 1, device, 2002) ==
         review::Result::SessionMismatch);
  assert(state.acknowledgeFromLocalTouch(2003) == review::Result::NotReady);
  assert(state.snapshot(2004).haFingerprint.empty());
  assert(state.snapshot(2004).verifiedSession == 0);

  // A duplicate proof, even with the identical fingerprint, must fail closed
  // instead of upgrading or reusing evidence as a second session.
  state.beginFromLocalTouch(3000);
  assert(state.observeVerifiedHa(session, ha, 3001) == review::Result::Ok);
  assert(state.observeVerifiedHa(session, ha, 3002) == review::Result::Replay);
  assert(state.observeVerifiedDevice(session, device, 3003) ==
         review::Result::NotReady);

  // Reject invalid digest shapes, including uppercase or untrusted strings.
  for (const auto &wrong : {
           std::string("bad"), std::string(64, 'A'),
           std::string(64, '/'), std::string(63, 'a'),
           std::string(65, 'a')}) {
    state.beginFromLocalTouch(4000);
    assert(state.observeVerifiedHa(session, wrong, 4001) ==
           review::Result::Invalid);
    assert(state.snapshot(4002).state == review::State::Rejected);
  }

  // Expiration protects unacknowledged evidence and scrubs fingerprints.
  state.beginFromLocalTouch(5000);
  assert(state.observeVerifiedHa(session, ha, 5001) == review::Result::Ok);
  assert(state.observeVerifiedDevice(session, device, 5002) ==
         review::Result::TouchRequired);
  assert(state.acknowledgeFromLocalTouch(5002 + review::kReviewMs) ==
         review::Result::Expired);
  assert(state.snapshot(5003 + review::kReviewMs).haFingerprint.empty());
  state.beginFromLocalTouch(100000);
  assert(state.observeVerifiedHa(session, ha, 100001) == review::Result::Ok);
  assert(state.observeVerifiedDevice(session, device, 100000 + review::kWindowMs) ==
         review::Result::Expired);

  state.beginFromLocalTouch(300000);
  assert(state.observeVerifiedHa(session, ha, 300001) == review::Result::Ok);
  state.rejectFromLocalTouch();
  assert(state.snapshot(300002).state == review::State::Rejected);
  assert(state.snapshot(300002).haFingerprint.empty());
  assert(state.acknowledgeFromLocalTouch(300003) == review::Result::NotReady);

  // Subtraction of uint32 uptime remains correct across rollover.
  state.beginFromLocalTouch(0xfffffff0u);
  assert(state.observeVerifiedHa(session, ha, 0xfffffff1u) ==
         review::Result::Ok);
  assert(state.observeVerifiedDevice(session, device, 10u) ==
         review::Result::TouchRequired);
  assert(state.acknowledgeFromLocalTouch(15u) ==
         review::Result::LocalReviewOnly);
  assert(!state.snapshot(16u).trustGranted);

  std::cout << "config v2 lab mutual same-session proof review policy: PASS\n";
}
