// Negative security-state tests for the inert Config v2 lifecycle.
// No real cryptography, HTTP pairing or deployment is exercised.
#include "../../lib/esp32HalImpl/wifiHandler/config_v2_transaction_lifecycle.hpp"
#include "../../lib/esp32HalImpl/wifiHandler/config_v2_pairing_policy.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

namespace tx = config_v2_transaction_lifecycle;
namespace pair = config_v2_pairing_policy;

static tx::AuthenticatedOffer validOffer() {
  return {{"waveshare-remote", "0123456789abcdef0123456789abcdef",
           std::string(64, 'a'), std::string(64, 'b')},
          std::string(32, 'c'), std::string(32, 'd')};
}
int main() {
  tx::Ledger ledger;
  config_v2_local_approval::Gate touch;
  const auto original = validOffer();
  auto forged = original;
  forged.adminActionId = original.authenticatedSessionId;
  assert(ledger.begin(forged, 100, 1000) == tx::Status::Invalid);
  forged = original;
  forged.adminActionId = original.request.transactionId;
  assert(ledger.begin(forged, 100, 1000) == tx::Status::Invalid);
  assert(ledger.begin(original, 100, 1000) == tx::Status::Ok);
  assert(ledger.validated(original, original.request.proposedSha256, 101) == tx::Status::Ok);
  assert(ledger.offerLocalTouch(original, touch, 102) == tx::Status::Ok);
  assert(ledger.readyToCommit(original, touch, 103) == tx::Status::PhysicalApprovalMissing);
  forged = original;
  forged.authenticatedSessionId = std::string(32, 'e');
  assert(ledger.readyToCommit(forged, touch, 104) == tx::Status::WrongSession);
  forged = original;
  forged.request.proposedSha256 = std::string(64, 'f');
  assert(ledger.readyToCommit(forged, touch, 104) == tx::Status::WrongRevision);
  assert(!touch.confirmLocalTouch(forged.request, 105));
  assert(touch.confirmLocalTouch(original.request, 105));
  assert(ledger.readyToCommit(original, touch, 106) == tx::Status::Ok);
  assert(!touch.consumeForAuthenticatedCommit(forged.request, 107));
  ledger.resetWithGate(touch);
  assert(!touch.consumeForAuthenticatedCommit(original.request, 108));
  assert(ledger.readyToCommit(original, touch, 109) == tx::Status::NotFound);

  pair::Window window;
  const pair::VerifiedPeer peer{std::string(64, 'a'), std::string(32, 'b')};
  assert(window.begin(pair::Entry::SettingsTouch, 200, [] { return 42u; }) == pair::Status::Ok);
  pair::VerifiedPeer unconfirmed;
  assert(window.takeConfirmedPeer(unconfirmed, 201) == pair::Status::NotConfirmed);
  assert(window.submitVerifiedPeerCode("000042", peer, 202) == pair::Status::Ok);
  assert(window.codeForLocalScreen(203).empty());
  window.cancel();
  assert(window.confirmTouch(204) == pair::Status::WrongPhase);
  assert(window.peerForLocalScreen(204) == nullptr);

  std::cout << "config v2 negative security state regressions: PASS\n";
}
