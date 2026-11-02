// Host regression tests for the inert, non-network transaction sequencer.
// These do not establish cryptographic authentication or activate NVS.
#include "../../lib/esp32HalImpl/wifiHandler/config_v2_transaction_lifecycle.hpp"
#include <cassert>
#include <cstdint>
#include <string>
#include <iostream>

using namespace config_v2_transaction_lifecycle;

static AuthenticatedOffer offer() {
  return {{"waveshare-remote", "0123456789abcdef0123456789abcdef",
           std::string(64, 'a'), std::string(64, 'b')},
          std::string(32, 'c'), std::string(32, 'd')};
}

int main() {
  auto o = offer();
  assert(offerValid(o));
  Ledger tx;
  config_v2_local_approval::Gate touch;

  assert(tx.phase(0) == Phase::Idle);
  assert(tx.begin(o, 100, 0) == Status::Invalid);
  assert(tx.begin(o, 100, kMaxTransactionMs + 1) == Status::Invalid);
  assert(tx.begin(o, 100, 5000) == Status::Ok);
  assert(tx.phase(100) == Phase::Uploading);
  assert(tx.begin(o, 101, 5000) == Status::Busy);
  assert(tx.readyToCommit(o, touch, 102) == Status::WrongPhase);
  assert(tx.offerLocalTouch(o, touch, 103) == Status::WrongPhase);

  auto other = o;
  other.authenticatedSessionId = std::string(32, 'e');
  assert(tx.validated(other, o.request.proposedSha256, 104) == Status::WrongSession);
  other = o;
  other.adminActionId = std::string(32, 'e');
  assert(tx.validated(other, o.request.proposedSha256, 104) == Status::WrongSession);
  other = o;
  other.request.proposedSha256 = std::string(64, 'f');
  assert(tx.validated(other, o.request.proposedSha256, 104) == Status::WrongRevision);
  other = o;
  other.request.expectedActiveSha256 = std::string(64, 'f');
  assert(tx.validated(other, o.request.proposedSha256, 104) == Status::WrongRevision);
  other = o;
  other.request.transactionId = std::string(32, 'f');
  assert(tx.validated(other, o.request.proposedSha256, 104) == Status::NotFound);

  assert(tx.validated(o, std::string(64, 'f'), 105) == Status::WrongRevision);
  assert(tx.phase(105) == Phase::Uploading);
  assert(tx.validated(o, o.request.proposedSha256, 106) == Status::Ok);
  assert(tx.phase(106) == Phase::Validated);
  assert(tx.validated(o, o.request.proposedSha256, 107) == Status::WrongPhase);
  assert(tx.offerLocalTouch(o, touch, 108) == Status::Ok);
  assert(tx.phase(108) == Phase::WaitingForTouch);
  assert(tx.readyToCommit(o, touch, 109) == Status::PhysicalApprovalMissing);
  assert(!touch.consumeForAuthenticatedCommit(o.request, 110));
  assert(touch.confirmLocalTouch(o.request, 111));
  assert(tx.readyToCommit(other, touch, 112) == Status::NotFound);
  assert(tx.readyToCommit(o, touch, 112) == Status::Ok);
  assert(tx.phase(112) == Phase::ReadyToCommit);
  assert(tx.readyToCommit(o, touch, 113) == Status::WrongPhase);
  // The ledger does NOT consume the physical approval. Actual NVS commit
  // must consume it atomically at config_v2_single_active_commit boundary.
  assert(touch.locallyApproved(114));
  assert(touch.consumeForAuthenticatedCommit(o.request, 115));
  assert(!touch.consumeForAuthenticatedCommit(o.request, 116));
  assert(tx.committed(o, 117) == Status::Ok);
  assert(tx.phase(117) == Phase::Consumed);
  assert(tx.committed(o, 118) == Status::WrongPhase);
  assert(tx.begin(o, 119, 5000) == Status::Busy);
  tx.resetWithGate(touch);
  assert(tx.phase(119) == Phase::Idle);

  // Reject forged duplicate security-context IDs, even if hashes are valid.
  auto invalid = o;
  invalid.authenticatedSessionId = invalid.request.transactionId;
  assert(!offerValid(invalid));
  invalid = o;
  invalid.adminActionId = invalid.authenticatedSessionId;
  assert(!offerValid(invalid));
  invalid = o;
  invalid.adminActionId = "not-a-random-128-bit-id";
  assert(!offerValid(invalid));
  invalid = o;
  invalid.request.expectedActiveSha256.clear(); // first legacy migration
  assert(offerValid(invalid));

  // Abort and disconnect always cancel the local gate; no touch approval
  // carries over to the next deployment.
  assert(tx.begin(o, 200, 1500) == Status::Ok);
  assert(tx.validated(o, o.request.proposedSha256, 201) == Status::Ok);
  assert(tx.offerLocalTouch(o, touch, 202) == Status::Ok);
  assert(tx.abort(other, touch, 203) == Status::NotFound);
  assert(tx.abort(o, touch, 204) == Status::Ok);
  assert(!touch.confirmLocalTouch(o.request, 205));
  assert(tx.phase(205) == Phase::Idle);
  assert(tx.begin(o, 206, 1500) == Status::Ok);
  tx.resetWithGate(touch);
  assert(tx.phase(207) == Phase::Idle);

  // A timed-out transaction fails closed and cannot finish via late touch.
  assert(tx.begin(o, 300, 10) == Status::Ok);
  assert(tx.validated(o, o.request.proposedSha256, 301) == Status::Ok);
  assert(tx.offerLocalTouch(o, touch, 302) == Status::Ok);
  assert(tx.readyToCommit(o, touch, 310) == Status::Expired);
  assert(!touch.confirmLocalTouch(o.request, 312)); // 8 ms window elapsed
  assert(tx.phase(312) == Phase::Idle);

  // Timer wraparound remains safe with a bounded 32-bit millisecond lease.
  constexpr std::uint32_t nearWrap = 0xfffffff0u;
  assert(tx.begin(o, nearWrap, 100) == Status::Ok);
  assert(tx.ownedBy(o, 10));
  assert(tx.phase(20) == Phase::Uploading);
  assert(tx.validated(o, o.request.proposedSha256, 21) == Status::Ok);
  assert(tx.offerLocalTouch(o, touch, 22) == Status::Ok);
  assert(!touch.confirmLocalTouch(o.request, 90));
  assert(tx.readyToCommit(o, touch, 90) == Status::Expired);

  std::cout << "config v2 transaction lifecycle: PASS\n";
}
