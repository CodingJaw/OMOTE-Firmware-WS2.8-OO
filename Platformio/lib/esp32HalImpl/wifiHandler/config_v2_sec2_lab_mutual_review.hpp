#pragma once
// LAB-ONLY volatile review of TWO independently cryptographically verified
// proofs received through the SAME encrypted Security2 session.
//
// This is a pure state machine: it performs NO cryptography, NO network I/O,
// NO NVS/HA credential writes, and grants NO authority. The transport must
// call observeVerifiedHa/Device ONLY after verifying its own proof. Methods
// cannot be exposed to public port-80 HTTP, MQTT, or serial commands.
// A UI may display and acknowledge this LAB evidence, never pair a remote.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace config_v2_sec2_lab_mutual_review {
constexpr std::uint32_t kWindowMs = 120000;
constexpr std::uint32_t kReviewMs = 60000;

enum class State {
  Closed, Collecting, HaOnly, DeviceOnly, AwaitingTouch, Reviewed,
  Rejected, Expired
};
enum class Result {
  Ok, NotReady, Invalid, SessionMismatch, Replay, Expired,
  TouchRequired, LocalReviewOnly
};

inline bool validFingerprint(std::string_view fingerprint) {
  if (fingerprint.size() != 64) return false;
  for (char c : fingerprint)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return false;
  return true;
}

struct Snapshot {
  State state = State::Closed;
  // Only populated for a fully matched HA+device proof awaiting/reviewed
  // on a single authenticated Security2 session; never grants authority.
  std::uint32_t verifiedSession = 0;
  std::string haFingerprint;
  std::string deviceFingerprint;
  bool trustGranted = false;
  bool deploymentEnabled = false;
};

class Review {
public:
  void beginFromLocalTouch(std::uint32_t now) {
    clear();
    openedAt_ = now;
    state_ = State::Collecting;
  }

  Result observeVerifiedHa(std::uint32_t session, std::string_view fingerprint,
                           std::uint32_t now) {
    return observe(session, fingerprint, now, true);
  }

  Result observeVerifiedDevice(std::uint32_t session,
                               std::string_view fingerprint,
                               std::uint32_t now) {
    return observe(session, fingerprint, now, false);
  }

  Snapshot snapshot(std::uint32_t now) {
    expire(now);
    Snapshot result;
    result.state = state_;
    if (state_ == State::AwaitingTouch || state_ == State::Reviewed) {
      result.verifiedSession = session_;
      result.haFingerprint = haFingerprint_;
      result.deviceFingerprint = deviceFingerprint_;
    }
    return result;
  }

  // Call ONLY from genuine local LVGL touchscreen action. A successful
  // return means the user reviewed two proof fingerprints, NOT that any
  // peer was enrolled or a trusted session was established.
  Result acknowledgeFromLocalTouch(std::uint32_t now) {
    expire(now);
    if (state_ == State::Expired) return Result::Expired;
    if (state_ != State::AwaitingTouch) return Result::NotReady;
    if (static_cast<std::uint32_t>(now - matchedAt_) >= kReviewMs) {
      setExpired();
      return Result::Expired;
    }
    state_ = State::Reviewed;
    return Result::LocalReviewOnly;
  }

  void rejectFromLocalTouch() {
    clear();
    state_ = State::Rejected;
  }

  void close() { clear(); }

private:
  Result observe(std::uint32_t session, std::string_view fingerprint,
                 std::uint32_t now, bool isHa) {
    expire(now);
    if (state_ == State::Expired) return Result::Expired;
    if (state_ != State::Collecting &&
        state_ != State::HaOnly && state_ != State::DeviceOnly)
      return Result::NotReady;
    if (!validFingerprint(fingerprint)) {
      clear();
      state_ = State::Rejected;
      return Result::Invalid;
    }

    if (sessionBound_ && session_ != session) {
      clear();
      state_ = State::Rejected;
      return Result::SessionMismatch;
    }
    if ((isHa && !haFingerprint_.empty()) ||
        (!isHa && !deviceFingerprint_.empty())) {
      clear();
      state_ = State::Rejected;
      return Result::Replay;
    }
    session_ = session;
    sessionBound_ = true;
    if (isHa) haFingerprint_.assign(fingerprint);
    else deviceFingerprint_.assign(fingerprint);
    if (!haFingerprint_.empty() && !deviceFingerprint_.empty()) {
      state_ = State::AwaitingTouch;
      matchedAt_ = now;
      return Result::TouchRequired;
    }
    state_ = isHa ? State::HaOnly : State::DeviceOnly;
    return Result::Ok;
  }

  void expire(std::uint32_t now) {
    if (state_ == State::Closed || state_ == State::Expired ||
        state_ == State::Rejected) return;
    if (static_cast<std::uint32_t>(now - openedAt_) >= kWindowMs ||
        (state_ == State::AwaitingTouch &&
         static_cast<std::uint32_t>(now - matchedAt_) >= kReviewMs)) {
      setExpired();
    }
  }

  void setExpired() {
    clear();
    state_ = State::Expired;
  }

  void clear() {
    haFingerprint_.clear();
    deviceFingerprint_.clear();
    session_ = 0;
    sessionBound_ = false;
    matchedAt_ = 0;
    openedAt_ = 0;
    state_ = State::Closed;
  }

  std::uint32_t openedAt_ = 0;
  std::uint32_t matchedAt_ = 0;
  std::uint32_t session_ = 0;
  bool sessionBound_ = false;
  State state_ = State::Closed;
  std::string haFingerprint_;
  std::string deviceFingerprint_;
};

} // namespace config_v2_sec2_lab_mutual_review
