#pragma once
// Config v2 device-side pairing orchestration (NO network handler).
//
// Three independent, fail-closed gates:
//  (1) a verified, encrypted provisional session bound to the peer public key
//  (2) a persisted attempt reservation + matching six-digit on-device PIN
//  (3) a NEW local touchscreen approval of the same live peer/session
//
// A real cryptographic Verifier is NOT implemented yet. A network handler
// MUST NOT construct VerifiedPeer from untrusted JSON, MAC or mDNS metadata.
// The production Verifier must prove the session and possession of the HA
// private key, enforce freshness/replay policy, and revoke stale sessions.
//
// The real touchscreen UI alone may call approveFromLocalTouch() or
// rejectFromLocalTouch(). Neither method may be exposed via HTTP/BLE/MQTT.
// The trust store is separate from the single-active configuration.
#include "config_v2_pairing_attempt_budget.hpp"
#include "config_v2_pairing_policy.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace config_v2_pairing_coordinator {

enum class Result {
  Ok,
  Busy,
  NotReady,
  TransportUnverified,
  RateLimited,
  StorageError,
  WrongCode,
  Expired,
  Rejected,
  PeerChanged,
  TouchRequired,
  PersistedButLocked
};

template <class Verifier, class AttemptGate, class TrustStore>
class Coordinator {
public:
  using Handle = typename Verifier::Handle;
  Coordinator(Verifier &verifier, AttemptGate &attempts, TrustStore &trust)
      : verifier_(verifier), attempts_(attempts), trust_(trust) {}

  template <class Csprng>
  config_v2_pairing_policy::Status begin(
      config_v2_pairing_policy::Entry origin, std::uint32_t now, Csprng random) {
    const auto status = window_.begin(origin, now, random);
    if (status == config_v2_pairing_policy::Status::Ok) {
      pending_ = Handle{};
      hasPending_ = false;
    }
    return status;
  }

  // Only the real LVGL screen may display the locally generated PIN.
  std::string codeForLocalScreen(std::uint32_t now) {
    return window_.codeForLocalScreen(now);
  }

  config_v2_pairing_policy::Phase phase(std::uint32_t now) {
    return window_.phase(now);
  }

  // Called by the FUTURE secure PAKE session manager, never public HTTP.
  Result submitFromAuthenticatedChannel(
      const Handle &handle, std::string_view candidate, std::uint32_t now) {
    if (window_.phase(now) != config_v2_pairing_policy::Phase::ShowCode)
      return Result::NotReady;

    config_v2_pairing_policy::VerifiedPeer verified;
    if (!verifier_.verifySession(handle, verified) ||
        !config_v2_pairing_policy::validPeer(verified))
      return Result::TransportUnverified;

    // Persistent reservation happens before ANY comparison of a candidate.
    const auto reservation = attempts_.reserve(now);
    if (reservation == config_v2_pairing_attempt_budget::Status::StorageError)
      return Result::StorageError;
    if (reservation != config_v2_pairing_attempt_budget::Status::Allowed) {
      window_.cancel();
      return Result::RateLimited;
    }

    const auto s = window_.submitVerifiedPeerCode(candidate, verified, now);
    if (s == config_v2_pairing_policy::Status::WrongCode ||
        s == config_v2_pairing_policy::Status::AttemptsExhausted) {
      if (s == config_v2_pairing_policy::Status::AttemptsExhausted)
        window_.cancel();
      return Result::WrongCode;
    }
    if (s == config_v2_pairing_policy::Status::Expired)
      return Result::Expired;
    if (s != config_v2_pairing_policy::Status::Ok)
      return Result::NotReady;

    pending_ = handle;
    hasPending_ = true;
    return Result::TouchRequired;
  }

  // The touchscreen must display the FULL authenticated fingerprint
  // (across multiple rows if necessary) before offering Approve / Reject.
  std::string pendingPeerFingerprint(std::uint32_t now) {
    if (!hasPending_ ||
        window_.phase(now) != config_v2_pairing_policy::Phase::AwaitingTouch)
      return {};
    const auto *peer = window_.peerForLocalScreen(now);
    return peer ? peer->publicKeyFingerprint : std::string();
  }

  // Genuine, fresh, local LVGL click only. Never a remote command.
  Result approveFromLocalTouch(std::uint32_t now) {
    if (!hasPending_ ||
        window_.phase(now) != config_v2_pairing_policy::Phase::AwaitingTouch)
      return Result::NotReady;
    const auto *initial = window_.peerForLocalScreen(now);
    if (!initial) return Result::Expired;

    config_v2_pairing_policy::VerifiedPeer currentlyProven;
    // Revalidate session possession immediately before recording trust.
    if (!verifier_.verifySession(pending_, currentlyProven) ||
        !config_v2_pairing_policy::validPeer(currentlyProven) ||
        currentlyProven.publicKeyFingerprint != initial->publicKeyFingerprint ||
        currentlyProven.secureSessionId != initial->secureSessionId) {
      cancel();
      return Result::PeerChanged;
    }
    if (window_.confirmTouch(now) != config_v2_pairing_policy::Status::Ok) {
      cancel();
      return Result::Expired;
    }
    config_v2_pairing_policy::VerifiedPeer approved;
    if (window_.takeConfirmedPeer(approved, now) !=
            config_v2_pairing_policy::Status::Ok ||
        approved.publicKeyFingerprint != currentlyProven.publicKeyFingerprint ||
        approved.secureSessionId != currentlyProven.secureSessionId) {
      cancel();
      return Result::PeerChanged;
    }

    // Re-pairing changes ONLY the HA trust key. Do not erase active config.
    // The trust write must be read-back verified by the actual NVS store.
    const bool persisted =
        trust_.replaceAfterAuthenticatedPhysicalApproval(approved.publicKeyFingerprint);
    pending_ = Handle{};
    hasPending_ = false;
    if (!persisted) return Result::StorageError;
    // If attempts cannot be cleared, fail shut; the peer pin is already
    // durably updated. Never falsely report complete success.
    if (attempts_.clearAfterDurableApprovedPairing() !=
        config_v2_pairing_attempt_budget::Status::Reset)
      return Result::PersistedButLocked;
    return Result::Ok;
  }

  Result rejectFromLocalTouch() {
    cancel();
    return Result::Rejected;
  }

  void cancel() {
    window_.cancel();
    pending_ = Handle{};
    hasPending_ = false;
  }

private:
  Verifier &verifier_;
  AttemptGate &attempts_;
  TrustStore &trust_;
  config_v2_pairing_policy::Window window_;
  Handle pending_{};
  bool hasPending_ = false;
};

} // namespace config_v2_pairing_coordinator
