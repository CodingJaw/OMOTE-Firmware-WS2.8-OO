#pragma once
// LAB ONLY: locally provisioned, persistent P-256 key for an encrypted
// challenge/response interoperability experiment.
//
// IMPORTANT: On the currently deployed Waveshare SDK configuration, ordinary
// NVS is not guaranteed encrypted. A software key stored here is COPYABLE by
// anyone with raw flash access and MUST NOT be used as a production device
// identity or accepted as a trusted device in HA. No eFuses are burned.
//
// Provisioning may be called ONLY from an intentional physical Settings tap,
// before the lab HTTPD listener starts. The HTTP endpoint can only sign a
// 32-byte ephemeral challenge for the current encrypted Security2 session;
// it cannot create, rotate, delete, export, enroll or authorize any identity.
#if defined(OMOTE_WAVESHARE28V2) && \
    defined(OMOTE_CONFIG_V2_PREVIEW) && \
    defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
#include <cstddef>
#include <cstdint>

namespace config_v2_sec2_lab_device_identity {
constexpr char kDomain[] = "OMOTE-SEC2-LAB-DEVICE-PROOF-V1";
constexpr std::size_t kPrivateBytes = 32;
constexpr std::size_t kPublicBytes = 65;
constexpr std::size_t kChallengeBytes = 32;
constexpr std::size_t kRequestBytes = 1 + kChallengeBytes;
constexpr std::size_t kMaxSignatureBytes = 80;
constexpr std::size_t kReplyHeaderBytes = 1 + kPublicBytes + 2;

class Identity {
public:
  Identity() = default;
  ~Identity() { clear(); }
  Identity(const Identity &) = delete;
  Identity &operator=(const Identity &) = delete;

  // Invoked only before the encrypted lab listener opens from local touch.
  // A pre-existing malformed record fails closed: NEVER replace silently.
  bool prepareFromTrustedLocalTouch();
  // Valid only during the same explicitly opened encrypted lab window.
  // Input: 0x01 || 32 random client bytes.
  // Output: 0x01 || 65-byte P-256 SEC1 public key ||
  //         u16 BE signature length || DER ECDSA-SHA256 signature.
  // Signed: domain || Security2 session id u32 BE || request nonce ||
  //         device public key.
  bool signSessionChallenge(
      std::uint32_t sessionId, const std::uint8_t *request,
      std::size_t requestLength, std::uint8_t *reply,
      std::size_t replyCapacity, std::size_t &replyLength) const;
  // TEST LAB ONLY: sign a SHA-256 digest computed locally from a fully
  // verified Security2 transaction. Never sign a caller-provided hash.
  // The v2 transport's caller must check the HA signature first.
#if defined(OMOTE_CONFIG_V2_SEC2_TX_V2_LAB)
  bool copyLabPublicKey(std::uint8_t *out65, std::size_t capacity) const;
  bool signVerifiedLabDigest(const std::uint8_t *digest32,
                             std::uint8_t *signature, std::size_t capacity,
                             std::size_t &signatureLength) const;
#endif
  void clear();
  bool ready() const { return ready_; }

private:
  std::uint8_t private_[kPrivateBytes]{};
  std::uint8_t public_[kPublicBytes]{};
  bool ready_ = false;
};
} // namespace config_v2_sec2_lab_device_identity
#endif
