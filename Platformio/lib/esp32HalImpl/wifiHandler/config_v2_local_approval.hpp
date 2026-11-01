#pragma once

// Firmware feature branch ONLY. Pure state machine for a mandatory *local*
// physical-touch approval on EACH future configuration deployment.
// This is NOT a pairing protocol, encrypted session or network handler.
// No caller in this branch currently grants remote writes.
//
// A request must first be authenticated by a future protocol before it may be
// offered here. Only a real touchscreen UI event may invoke confirmLocalTouch.
// Host-controlled HTTP, mDNS, MQTT, BLE and WebSocket routes MUST NEVER call
// confirmLocalTouch or directly set the approval bit.
#include <cstdint>
#include <string>
#include <utility>

namespace config_v2_local_approval {

constexpr std::uint32_t kMaximumApprovalMs = 60'000;

struct Request {
  std::string deviceId;
  std::string transactionId;
  std::string proposedSha256;
  std::string expectedActiveSha256;
};

inline bool sha256IsValid(const std::string &hash) {
  if (hash.size() != 64)
    return false;
  for (char ch : hash)
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
      return false;
  return true;
}

inline bool transactionIsValid(const std::string &value) {
  if (value.size() != 32)
    return false;
  for (char ch : value)
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
      return false;
  return true;
}

inline bool requestIsValid(const Request &req) {
  return !req.deviceId.empty() && req.deviceId.size() <= 128 &&
         transactionIsValid(req.transactionId) &&
         sha256IsValid(req.proposedSha256) &&
         sha256IsValid(req.expectedActiveSha256) &&
         req.proposedSha256 != req.expectedActiveSha256;
}

inline bool sameRequest(const Request &a, const Request &b) {
  return a.deviceId == b.deviceId &&
         a.transactionId == b.transactionId &&
         a.proposedSha256 == b.proposedSha256 &&
         a.expectedActiveSha256 == b.expectedActiveSha256;
}

class Gate {
public:
  // The authenticated server may *offer* a transaction for local review,
  // but it MUST NOT call confirmLocalTouch or authorize a network commit.
  bool offer(const Request &req, std::uint32_t nowMs,
             std::uint32_t approvalWindowMs) {
    if (active_ || !requestIsValid(req) || approvalWindowMs == 0 ||
        approvalWindowMs > kMaximumApprovalMs)
      return false;
    request_ = req;
    offeredAtMs_ = nowMs;
    durationMs_ = approvalWindowMs;
    active_ = true;
    approved_ = false;
    return true;
  }

  // Call exclusively from an explicit touchscreen confirmation callback.
  bool confirmLocalTouch(const Request &req, std::uint32_t nowMs) {
    if (!isPending(req, nowMs) || approved_)
      return false;
    approved_ = true;
    return true;
  }

  // A network-side commit path still needs independent HA administrator
  // authentication, a fresh active-SHA precondition and an exact transaction
  // match. This call is single-use and consumes local approval.
  bool consumeForAuthenticatedCommit(const Request &req,
                                     std::uint32_t nowMs) {
    if (!isPending(req, nowMs) || !approved_)
      return false;
    clear();
    return true;
  }

  bool waitingForTouch(std::uint32_t nowMs) {
    if (expired(nowMs)) {
      clear();
      return false;
    }
    return active_ && !approved_;
  }

  bool locallyApproved(std::uint32_t nowMs) {
    if (expired(nowMs)) {
      clear();
      return false;
    }
    return active_ && approved_;
  }

  // Local cancel, network disconnect, app reboot or invalid state may clear.
  // No automatic acceptance is ever performed on timeout.
  void clear() {
    request_ = Request{};
    offeredAtMs_ = 0;
    durationMs_ = 0;
    active_ = false;
    approved_ = false;
  }

private:
  bool expired(std::uint32_t nowMs) const {
    return active_ &&
           static_cast<std::uint32_t>(nowMs - offeredAtMs_) >= durationMs_;
  }

  bool isPending(const Request &req, std::uint32_t nowMs) {
    if (expired(nowMs)) {
      clear();
      return false;
    }
    return active_ && sameRequest(request_, req);
  }

  Request request_{};
  std::uint32_t offeredAtMs_ = 0;
  std::uint32_t durationMs_ = 0;
  bool active_ = false;
  bool approved_ = false;
};

} // namespace config_v2_local_approval
