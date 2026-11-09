// Host-only LAB review lifecycle safety regressions.
// Simulates ALREADY verified cryptographic facts; does not send network
// requests, accept PINs, write NVS, grant trust, or enable deployments.
#include "../../lib/esp32HalImpl/wifiHandler/config_v2_sec2_lab_mutual_review.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

namespace review = config_v2_sec2_lab_mutual_review;
namespace {
const std::string kHa(64, 'a');
const std::string kDevice(64, 'b');
constexpr std::uint32_t kSession = 0x11773355u;

void assertScrubbed(review::Review &r, std::uint32_t now,
                    review::State expected) {
  const auto s=r.snapshot(now);
  assert(s.state==expected);
  assert(s.verifiedSession==0);
  assert(s.haFingerprint.empty());
  assert(s.deviceFingerprint.empty());
  assert(!s.trustGranted);
  assert(!s.deploymentEnabled);
}

void populate(review::Review &r, std::uint32_t started) {
  r.beginFromLocalTouch(started);
  assert(r.observeVerifiedHa(kSession,kHa,started+1)==review::Result::Ok);
  assert(r.observeVerifiedDevice(kSession,kDevice,started+2)==
         review::Result::TouchRequired);
  const auto snapshot=r.snapshot(started+3);
  assert(snapshot.state==review::State::AwaitingTouch);
  assert(snapshot.verifiedSession==kSession);
  assert(!snapshot.trustGranted && !snapshot.deploymentEnabled);
}

void testPendingReviewDeadline() {
  review::Review r;
  populate(r,1000);
  // The review countdown starts on the SECOND verified proof, at +2 ms.
  assert(r.snapshot(1002+review::kReviewMs-1).state==
         review::State::AwaitingTouch);
  assert(r.acknowledgeFromLocalTouch(1002+review::kReviewMs)==
         review::Result::Expired);
  assertScrubbed(r,1002+review::kReviewMs,review::State::Expired);
  assert(r.acknowledgeFromLocalTouch(1002+review::kReviewMs+1)==
         review::Result::Expired);
}

void testWholeWindowExpirationEvenAfterReview() {
  review::Review r;
  populate(r,10000);
  assert(r.acknowledgeFromLocalTouch(10005)==
         review::Result::LocalReviewOnly);
  const auto approved=r.snapshot(10006);
  assert(approved.state==review::State::Reviewed);
  assert(approved.verifiedSession==kSession);
  // A local Review OK NEVER makes the evidence permanent or trusted.
  assert(!approved.trustGranted && !approved.deploymentEnabled);
  assert(r.snapshot(10000+review::kWindowMs-1).state==
         review::State::Reviewed);
  assertScrubbed(r,10000+review::kWindowMs,review::State::Expired);
  assert(r.observeVerifiedHa(kSession,kHa,10000+review::kWindowMs+1)==
         review::Result::Expired);
}

void testLocalRejectScrubsSameSessionAndNeverReapproves() {
  review::Review r;
  populate(r,2000);
  r.rejectFromLocalTouch();
  assertScrubbed(r,2004,review::State::Rejected);
  assert(r.acknowledgeFromLocalTouch(2005)==review::Result::NotReady);
  assert(r.observeVerifiedDevice(kSession,kDevice,2006)==
         review::Result::NotReady);
  assertScrubbed(r,2007,review::State::Rejected);
  // Starting a completely new physical window must not restore old proof.
  r.beginFromLocalTouch(2500);
  assertScrubbed(r,2501,review::State::Collecting);
  assert(r.acknowledgeFromLocalTouch(2502)==review::Result::NotReady);
}

void testCloseAndRearmNeverReusesOldFingerprints() {
  review::Review r;
  populate(r,3000);
  assert(r.acknowledgeFromLocalTouch(3004)==
         review::Result::LocalReviewOnly);
  r.close();
  assertScrubbed(r,3005,review::State::Closed);
  assert(r.observeVerifiedHa(kSession,kHa,3006)==review::Result::NotReady);
  r.beginFromLocalTouch(4000);
  assertScrubbed(r,4001,review::State::Collecting);
  // No peer is displayed until BOTH proofs are independently revalidated.
  assert(r.observeVerifiedDevice(kSession+1,kDevice,4002)==
         review::Result::Ok);
  assertScrubbed(r,4003,review::State::DeviceOnly);
  assert(r.observeVerifiedHa(kSession,kHa,4004)==
         review::Result::SessionMismatch);
  assertScrubbed(r,4005,review::State::Rejected);
}

void testUptimeRolloverExpiration() {
  review::Review r;
  const auto start=0xffffffe0u;
  r.beginFromLocalTouch(start);
  assert(r.observeVerifiedHa(kSession,kHa,start+1)==review::Result::Ok);
  assert(r.observeVerifiedDevice(kSession,kDevice,start+2)==
         review::Result::TouchRequired);
  assert(r.acknowledgeFromLocalTouch(start+3)==
         review::Result::LocalReviewOnly);
  assertScrubbed(r,start+review::kWindowMs,review::State::Expired);
}
} // namespace

int main() {
  testPendingReviewDeadline();
  testWholeWindowExpirationEvenAfterReview();
  testLocalRejectScrubsSameSessionAndNeverReapproves();
  testCloseAndRearmNeverReusesOldFingerprints();
  testUptimeRolloverExpiration();
  std::cout<<"Security2 LAB verified review expiry/reject/rearm: PASS\n";
  return 0;
}
