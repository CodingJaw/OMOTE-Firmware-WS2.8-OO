#include "../../lib/esp32HalImpl/wifiHandler/config_v2_local_approval.hpp"
#include <cassert>
#include <cstdint>
#include <string>

using config_v2_local_approval::Gate;
using config_v2_local_approval::Request;
using config_v2_local_approval::requestIsValid;

static Request proposed() {
  return {"omote-device-1", "0123456789abcdef0123456789abcdef",
          std::string(64, 'a'), std::string(64, 'b')};
}

int main() {
  const Request r = proposed();
  assert(requestIsValid(r));

  Gate gate;
  assert(!gate.consumeForAuthenticatedCommit(r, 100));
  assert(!gate.confirmLocalTouch(r, 100));
  assert(!gate.offer(r, 100, 0));
  assert(!gate.offer(r, 100, 60001));
  assert(gate.offer(r, 100, 30000));
  assert(gate.waitingForTouch(100));
  assert(!gate.locallyApproved(100));
  assert(!gate.offer(r, 101, 500));

  Request wrong = r;
  wrong.transactionId[0] = 'f';
  assert(!gate.confirmLocalTouch(wrong, 102));
  assert(!gate.consumeForAuthenticatedCommit(wrong, 103));
  wrong = r;
  wrong.deviceId = "other-remote";
  assert(!gate.confirmLocalTouch(wrong, 104));
  wrong = r;
  wrong.proposedSha256[0] = 'c';
  assert(!gate.confirmLocalTouch(wrong, 105));
  wrong = r;
  wrong.expectedActiveSha256[0] = 'd';
  assert(!gate.confirmLocalTouch(wrong, 106));

  assert(!gate.consumeForAuthenticatedCommit(r, 108));
  assert(gate.confirmLocalTouch(r, 109));
  assert(gate.locallyApproved(110));
  assert(!gate.confirmLocalTouch(r, 111));
  assert(!gate.consumeForAuthenticatedCommit(wrong, 112));
  assert(gate.consumeForAuthenticatedCommit(r, 113));
  assert(!gate.locallyApproved(114));
  assert(!gate.consumeForAuthenticatedCommit(r, 115));
  assert(!gate.confirmLocalTouch(r, 116));

  // Timeout always rejects, including when touch approval already occurred.
  assert(gate.offer(r, 1000, 500));
  assert(gate.confirmLocalTouch(r, 1499));
  assert(!gate.consumeForAuthenticatedCommit(r, 1500));
  assert(!gate.waitingForTouch(1501));

  // A second deployment must require a second explicit local touch.
  assert(gate.offer(r, 2000, 100));
  assert(!gate.consumeForAuthenticatedCommit(r, 2001));
  assert(gate.confirmLocalTouch(r, 2002));
  gate.clear();
  assert(!gate.consumeForAuthenticatedCommit(r, 2003));


  // Timer wraparound: expire correctly for a short bounded window.
  constexpr std::uint32_t nearWrap = 0xfffffffa;
  assert(gate.offer(r, nearWrap, 100));
  assert(gate.waitingForTouch(15));
  assert(!gate.confirmLocalTouch(r, 95));
  assert(!gate.consumeForAuthenticatedCommit(r, 95));

  Request invalid = r;
  invalid.transactionId = "short";
  assert(!gate.offer(invalid, 1, 1000));
  invalid = r;
  invalid.proposedSha256 = std::string(64, 'g');
  assert(!gate.offer(invalid, 1, 1000));
  invalid = r;
  invalid.proposedSha256 = invalid.expectedActiveSha256;
  assert(!gate.offer(invalid, 1, 1000));
}
