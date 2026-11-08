#pragma once
// OPT-IN Security2 TX-V2 laboratory-only coordinator. Never a pairing grant.
// Compiled only when both OMOTE_CONFIG_V2_SEC2_LAB_PROBE and
// OMOTE_CONFIG_V2_SEC2_TX_V2_LAB are explicitly enabled.
#if defined(OMOTE_WAVESHARE28V2) && defined(OMOTE_CONFIG_V2_PREVIEW) && \
    defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE) && \
    defined(OMOTE_CONFIG_V2_SEC2_TX_V2_LAB)
#include "config_v2_sec2_lab_device_identity.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace config_v2_sec2_tx_v2_lab {
constexpr std::size_t kOpenRequestBytes = 1 + 65 + 32;
constexpr std::size_t kOpenReplyBytes = 1 + 4 + 4 + 16 + 1 + 8 + 65 + 32;
constexpr std::size_t kMaxCommitRequestBytes = 1 + 1 + 80;
constexpr std::uint32_t kTransactionTimeoutMs = 30000;
constexpr std::uint32_t kLabEpoch = 1;
constexpr std::uint8_t kProtocolVersion = 2;

// Populated only after both signatures and the bound transcript are verified.
// These are PUBLIC keys, never identity credentials or pairing authorization.
struct VerifiedPublicKeys {
  bool valid = false;
  std::array<std::uint8_t,65> ha{};
  std::array<std::uint8_t,65> device{};
};

class Coordinator {
public:
  bool open(std::uint32_t session, const std::uint8_t *request,
            std::size_t requestLength, std::uint32_t nowMs,
            const config_v2_sec2_lab_device_identity::Identity &identity,
            std::vector<std::uint8_t> &reply);
  bool commit(std::uint32_t session, const std::uint8_t *request,
              std::size_t requestLength, std::uint32_t nowMs,
              const config_v2_sec2_lab_device_identity::Identity &identity,
              std::vector<std::uint8_t> &reply,
              VerifiedPublicKeys &verified);
  // Non-authorizing proof lookup, restricted to the SAME Security2 session
  // and transcript digest as a completed mutual-signature transaction.
  bool matchesCompleted(std::uint32_t session,
                        const std::uint8_t *digest32);
  void close();

private:
  struct Pending {
    bool active = false;
    std::uint32_t session = 0;
    std::uint32_t created = 0;
    std::array<std::uint8_t,16> tx{};
    std::array<std::uint8_t,8> deviceId{};
    std::array<std::uint8_t,65> haPub{};
    std::array<std::uint8_t,65> devicePub{};
    std::array<std::uint8_t,32> haNonce{};
    std::array<std::uint8_t,32> deviceNonce{};
  };
  std::mutex mutex_;
  Pending pending_{};
  std::uint32_t completedSession_ = 0;
  std::array<std::uint8_t,32> completedDigest_{};
};
} // namespace config_v2_sec2_tx_v2_lab
#endif
