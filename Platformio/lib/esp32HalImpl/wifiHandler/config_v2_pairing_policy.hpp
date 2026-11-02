#pragma once
// Local-only first-pair/re-pair interaction for OMOTE Config v2.
//
// This is a STATE MACHINE, not a key exchange or a network pairing server.
// No caller in the preview firmware may accept a pairing code over HTTP.
// A future mutually encrypted *provisional* channel must prove possession
// of the candidate peer's public key BEFORE a trusted manager calls
// submitVerifiedPeerCode(). A six-digit code alone is NEVER authentication.
//
// User-selected UX: either Settings touchscreen or a future physical keypad
// chord may open the window; local six-digit code is entered in HA; the same
// remote touchscreen must then confirm the exact peer. Re-pairing replaces
// credentials only by a separate explicit trust-store operation and must
// preserve the active configuration.
#include "config_v2_digest.hpp"
#include "config_v2_stage_policy.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace config_v2_pairing_policy {

constexpr std::uint32_t kWindowMs = 120000;
constexpr std::uint32_t kTouchWindowMs = 60000;
constexpr std::uint32_t kCooldownMs = 120000;
constexpr unsigned kMaximumWrongCodes = 5;
// 2^32 reduced to a multiple of 1,000,000 for unbiased six-digit codes.
constexpr std::uint64_t kUniformLimit = 4294000000ULL;

enum class Entry { SettingsTouch, PhysicalKeyChord };
enum class Phase { Idle, ShowCode, AwaitingTouch, Confirmed };
enum class Status {
  Ok, Invalid, Busy, Cooldown, EntropyUnavailable, WrongCode,
  AttemptsExhausted, WrongPhase, Expired, NotConfirmed
};

struct VerifiedPeer {
  // SHA-256 hex fingerprint of a peer public key already authenticated by
  // a trusted provisional encrypted key agreement, NEVER an mDNS/MAC hint.
  std::string publicKeyFingerprint;
  // Unique identifier of the proven provisional secure transport session.
  std::string secureSessionId;
};

inline bool validPeer(const VerifiedPeer &peer) {
  return config_v2_digest::valid(peer.publicKeyFingerprint) &&
         config_v2_stage_policy::transactionIdValid(peer.secureSessionId);
}

class Window {
public:
  // Draw must return independent CSPRNG-generated uint32_t values. The ESP
  // adapter supplies esp_random(), not millis()/Arduino random(). Bounded
  // rejection sampling fails closed if its entropy source malfunctions.
  template <typename Draw>
  Status begin(Entry entry, std::uint32_t now, Draw draw) {
    if (coolingDown(now))
      return Status::Cooldown;
    if (phase_ != Phase::Idle && !expired(now))
      return Status::Busy;
    clearWindow();
    std::uint32_t sample = 0;
    bool accepted = false;
    for (unsigned i = 0; i < 32; ++i) {
      sample = static_cast<std::uint32_t>(draw());
      if (static_cast<std::uint64_t>(sample) < kUniformLimit) {
        accepted = true;
        break;
      }
    }
    if (!accepted)
      return Status::EntropyUnavailable;
    const std::uint32_t code = sample % 1000000u;
    for (int pos = 5; pos >= 0; --pos) {
      code_[static_cast<std::size_t>(pos)] =
          static_cast<char>('0' + (code / decimalPower(static_cast<unsigned>(5 - pos))) % 10u);
    }
    code_[6] = '\0';
    entry_ = entry;
    startedAt_ = now;
    phase_ = Phase::ShowCode;
    return Status::Ok;
  }

  // Only the local LVGL page may display this code. Do not expose this
  // method through HTTP, logs, MQTT, mDNS or crash diagnostics.
  std::string codeForLocalScreen(std::uint32_t now) {
    expireIfNeeded(now);
    return phase_ == Phase::ShowCode ? std::string(code_.data(), 6) : std::string();
  }

  // Only the authenticated provisional-channel manager can call this. It
  // must already have bound peer.publicKeyFingerprint to possession of a
  // peer private key and peer.secureSessionId to the encrypted session.
  Status submitVerifiedPeerCode(std::string_view candidate,
                                const VerifiedPeer &peer,
                                std::uint32_t now) {
    if (coolingDown(now))
      return Status::Cooldown;
    if (expired(now)) {
      clearWindow();
      return Status::Expired;
    }
    if (phase_ != Phase::ShowCode)
      return Status::WrongPhase;
    if (!validPeer(peer))
      return Status::Invalid;
    unsigned mismatch = static_cast<unsigned>(candidate.size() != 6);
    for (std::size_t i = 0; i < 6; ++i) {
      const unsigned char digit =
          i < candidate.size() ? static_cast<unsigned char>(candidate[i]) : 0u;
      mismatch |= static_cast<unsigned>(digit ^ static_cast<unsigned char>(code_[i]));
    }
    if (mismatch != 0) {
      ++wrongCodes_;
      if (wrongCodes_ >= kMaximumWrongCodes) {
        clearWindow();
        cooldownStartedAt_ = now;
        cooldownActive_ = true;
        return Status::AttemptsExhausted;
      }
      return Status::WrongCode;
    }
    peer_ = peer;
    secureProofAt_ = now;
    phase_ = Phase::AwaitingTouch;
    wipeCode();
    return Status::Ok;
  }

  // Exclusively an explicit on-device LVGL touchscreen Approve callback.
  // The caller MUST display the bound peer fingerprint before calling.
  Status confirmTouch(std::uint32_t now) {
    if (expired(now)) {
      clearWindow();
      return Status::Expired;
    }
    if (phase_ != Phase::AwaitingTouch)
      return Status::WrongPhase;
    if (static_cast<std::uint32_t>(now - secureProofAt_) >= kTouchWindowMs) {
      clearWindow();
      return Status::Expired;
    }
    phase_ = Phase::Confirmed;
    return Status::Ok;
  }

  // A trusted manager may *take* this one-shot approval to persist a vetted
  // peer public key in a separate protected trust store. This class does not
  // write keys, NVS, credential files, configuration, or HA state.
  Status takeConfirmedPeer(VerifiedPeer &out, std::uint32_t now) {
    if (expired(now)) {
      clearWindow();
      return Status::Expired;
    }
    if (phase_ != Phase::Confirmed)
      return Status::NotConfirmed;
    out = peer_;
    clearWindow();
    return Status::Ok;
  }

  void cancel() { clearWindow(); }
  Phase phase(std::uint32_t now) {
    expireIfNeeded(now);
    return phase_;
  }
  Entry entry() const { return entry_; }
  bool lockedOut(std::uint32_t now) { return coolingDown(now); }
  unsigned failedAttempts() const { return wrongCodes_; }
  const VerifiedPeer *peerForLocalScreen(std::uint32_t now) {
    expireIfNeeded(now);
    return phase_ == Phase::AwaitingTouch || phase_ == Phase::Confirmed ?
        &peer_ : nullptr;
  }

private:
  static std::uint32_t decimalPower(unsigned exp) {
    constexpr std::uint32_t pow10[] = {1u, 10u, 100u, 1000u, 10000u, 100000u};
    return pow10[exp];
  }
  bool expired(std::uint32_t now) const {
    return phase_ != Phase::Idle &&
           static_cast<std::uint32_t>(now - startedAt_) >= kWindowMs;
  }
  void expireIfNeeded(std::uint32_t now) {
    if (expired(now))
      clearWindow();
  }
  bool coolingDown(std::uint32_t now) {
    if (!cooldownActive_)
      return false;
    if (static_cast<std::uint32_t>(now - cooldownStartedAt_) >= kCooldownMs) {
      cooldownActive_ = false;
      return false;
    }
    return true;
  }
  void wipeCode() {
    volatile char *p = code_.data();
    for (std::size_t i = 0; i < code_.size(); ++i)
      p[i] = 0;
  }
  void clearWindow() {
    wipeCode();
    peer_ = VerifiedPeer{};
    startedAt_ = 0;
    secureProofAt_ = 0;
    wrongCodes_ = 0;
    phase_ = Phase::Idle;
    // Cooldown survives cancellation within the process. Production must
    // additionally persist attempt limits against power-cycle brute force.
  }
  std::array<char, 7> code_{};
  VerifiedPeer peer_{};
  Phase phase_ = Phase::Idle;
  Entry entry_ = Entry::SettingsTouch;
  std::uint32_t startedAt_ = 0;
  std::uint32_t secureProofAt_ = 0;
  std::uint32_t cooldownStartedAt_ = 0;
  bool cooldownActive_ = false;
  unsigned wrongCodes_ = 0;
};

} // namespace config_v2_pairing_policy
