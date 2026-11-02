#pragma once

// Config v2 transaction sequencing policy. Host-testable; NO crypto, disk I/O,
// HTTP routes, authentication verification, or NVS activation. Inert until a
// future authenticated manager owns it. It must NEVER be wired directly to
// an unauthenticated endpoint.
//
// The manager must verify encrypted session/device binding and a *fresh*
// HA administrator authorization BEFORE begin(), and it must reverify that
// authorization, persisted expected-active SHA, file hashes and graph BEFORE
// calling the separate single-active commit primitive. Calling validated()
// below does not verify files; it records a result from a trusted validator.
//
// Local confirmation is exclusively via the LVGL touchscreen event calling
// config_v2_local_approval::Gate::confirmLocalTouch. This policy does not
// expose or invoke any physical-confirmation setter.
#include "config_v2_local_approval.hpp"
#include "config_v2_stage_policy.hpp"
#include <cstdint>
#include <string>
#include <utility>

namespace config_v2_transaction_lifecycle {

constexpr std::uint32_t kMaxTransactionMs = 10u * 60u * 1000u;
constexpr std::uint32_t kTouchWindowMs = 60u * 1000u;

enum class Phase { Idle, Uploading, Validated, WaitingForTouch, ReadyToCommit, Consumed };
enum class Status {
  Ok, Invalid, Busy, Expired, NotFound, WrongSession, WrongRevision,
  WrongPhase, PhysicalApprovalMissing
};

struct AuthenticatedOffer {
  config_v2_local_approval::Request request;
  // Opaque ID of a cryptographically verified, device-bound secure session.
  // The policy does not authenticate this ID: only a trusted manager may set it.
  std::string authenticatedSessionId;
  // Opaque unique ID for a fresh *HA-admin-approved* deploy action,
  // independently validated by the trusted manager. Must not be just a
  // pairing token or a persistent HA credential.
  std::string adminActionId;
};

inline bool offerValid(const AuthenticatedOffer &offer) {
  return config_v2_local_approval::requestIsValid(offer.request) &&
         config_v2_stage_policy::transactionIdValid(offer.authenticatedSessionId) &&
         config_v2_stage_policy::transactionIdValid(offer.adminActionId) &&
         offer.authenticatedSessionId != offer.adminActionId &&
         offer.authenticatedSessionId != offer.request.transactionId &&
         offer.adminActionId != offer.request.transactionId;
}

class Ledger {
public:
  Status begin(const AuthenticatedOffer &offer, std::uint32_t now,
               std::uint32_t lifetimeMs = kMaxTransactionMs) {
    expireIfNeeded(now);
    if (phase_ != Phase::Idle)
      return Status::Busy;
    if (!offerValid(offer) || lifetimeMs == 0 ||
        lifetimeMs > kMaxTransactionMs)
      return Status::Invalid;
    offer_ = offer;
    issuedAt_ = now;
    durationMs_ = lifetimeMs;
    phase_ = Phase::Uploading;
    return Status::Ok;
  }

  // The trusted manager may call this ONLY after all staged files, checksums,
  // canonical manifest SHA and complete graph have actually been checked.
  Status validated(const AuthenticatedOffer &offer,
                   const std::string &verifiedManifestSha,
                   std::uint32_t now) {
    const Status state = check(offer, now);
    if (state != Status::Ok)
      return state;
    if (phase_ != Phase::Uploading)
      return Status::WrongPhase;
    if (verifiedManifestSha != offer_.request.proposedSha256)
      return Status::WrongRevision;
    phase_ = Phase::Validated;
    return Status::Ok;
  }

  // Offer physical approval only AFTER validation and authenticated admin
  // consent. This call cannot approve itself; the actual user must touch.
  Status offerLocalTouch(const AuthenticatedOffer &offer,
                         config_v2_local_approval::Gate &gate,
                         std::uint32_t now) {
    const Status state = check(offer, now);
    if (state != Status::Ok)
      return state;
    if (phase_ != Phase::Validated)
      return Status::WrongPhase;
    const std::uint32_t elapsed = now - issuedAt_;
    const std::uint32_t left = durationMs_ - elapsed;
    const std::uint32_t window =
        left < kTouchWindowMs ? left : kTouchWindowMs;
    if (!window || !gate.offer(offer_.request, now, window))
      return Status::PhysicalApprovalMissing;
    phase_ = Phase::WaitingForTouch;
    return Status::Ok;
  }

  // Recheck the local gate and the exact authenticated request. A success
  // provides only a single-use sequencing grant to a *trusted* manager.
  // It is NOT cryptographic authorization and does NOT activate NVS.
  Status consumeLocalApproval(const AuthenticatedOffer &offer,
                              config_v2_local_approval::Gate &gate,
                              std::uint32_t now) {
    const Status state = check(offer, now);
    if (state != Status::Ok)
      return state;
    if (phase_ != Phase::WaitingForTouch)
      return Status::WrongPhase;
    if (!gate.consumeForAuthenticatedCommit(offer_.request, now))
      return Status::PhysicalApprovalMissing;
    phase_ = Phase::Consumed;
    return Status::Ok;
  }

  // One outstanding request per device. Aborts clear the gate; a disconnected
  // session must abort rather than leaving touchscreen approval outstanding.
  Status abort(const AuthenticatedOffer &offer,
               config_v2_local_approval::Gate &gate,
               std::uint32_t now) {
    const Status state = check(offer, now);
    if (state != Status::Ok)
      return state;
    gate.clear();
    reset();
    return Status::Ok;
  }

  // Called by the trusted manager on session disconnect and on boot.
  void resetWithGate(config_v2_local_approval::Gate &gate) {
    gate.clear();
    reset();
  }

  Phase phase(std::uint32_t now) {
    expireIfNeeded(now);
    return phase_;
  }

  // True when this exact verified session and HA-admin action own the lease.
  // No raw transaction ID alone can authorize operations.
  bool ownedBy(const AuthenticatedOffer &offer, std::uint32_t now) {
    return check(offer, now) == Status::Ok;
  }

private:
  Status check(const AuthenticatedOffer &candidate, std::uint32_t now) {
    if (phase_ == Phase::Idle)
      return Status::NotFound;
    if (expired(now)) {
      reset();
      return Status::Expired;
    }
    if (candidate.request.transactionId != offer_.request.transactionId)
      return Status::NotFound;
    if (candidate.authenticatedSessionId != offer_.authenticatedSessionId ||
        candidate.adminActionId != offer_.adminActionId)
      return Status::WrongSession;
    if (!config_v2_local_approval::sameRequest(candidate.request,
                                               offer_.request))
      return Status::WrongRevision;
    return Status::Ok;
  }

  bool expired(std::uint32_t now) const {
    return phase_ != Phase::Idle &&
           static_cast<std::uint32_t>(now - issuedAt_) >= durationMs_;
  }

  void expireIfNeeded(std::uint32_t now) {
    if (expired(now))
      reset();
  }

  void reset() {
    offer_ = AuthenticatedOffer{};
    phase_ = Phase::Idle;
    issuedAt_ = 0;
    durationMs_ = 0;
  }

  AuthenticatedOffer offer_{};
  Phase phase_ = Phase::Idle;
  std::uint32_t issuedAt_ = 0;
  std::uint32_t durationMs_ = 0;
};

} // namespace config_v2_transaction_lifecycle
