// One encrypted lab endpoint's P-256 candidate key-possession check.
// No enrollment, authorization or trusted state changes in this module.
#if defined(OMOTE_WAVESHARE28V2) && \
    defined(OMOTE_CONFIG_V2_PREVIEW) && \
    defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
#include "config_v2_sec2_lab_peer_proof.hpp"
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>
#include <array>
#include <cstring>

namespace config_v2_sec2_lab_peer_proof {

bool verifyCandidate(uint32_t authenticatedSec2Session,
                     const uint8_t *request, size_t requestLength) {
  if (request == nullptr || requestLength < kHeaderLength + 8 ||
      requestLength > kHeaderLength + kMaxSignatureLength ||
      request[0] != 0x01 || request[1] != 0x04) {
    return false;
  }
  constexpr std::size_t kPublicOffset = 1;
  constexpr std::size_t kNonceOffset = kPublicOffset + kPubLength;
  constexpr std::size_t kSignatureLengthOffset = kNonceOffset + kNonceLength;
  const std::size_t sigLength =
      (static_cast<std::size_t>(request[kSignatureLengthOffset]) << 8) |
      static_cast<std::size_t>(request[kSignatureLengthOffset + 1]);
  if (sigLength < 8 || sigLength > kMaxSignatureLength ||
      requestLength != kHeaderLength + sigLength)
    return false;

  // Bind this candidate-key proof to the exact encrypted Security2 session.
  // A new session ID and fresh client nonce make captured signatures unusable
  // for other windows; AES-GCM counters separately protect transport replay.
  constexpr std::size_t kDomainLength = sizeof(kDomain) - 1;
  std::array<uint8_t, kDomainLength + 4 + kPubLength + kNonceLength> signedData{};
  std::memcpy(signedData.data(), kDomain, kDomainLength);
  signedData[kDomainLength + 0] =
      static_cast<uint8_t>(authenticatedSec2Session >> 24);
  signedData[kDomainLength + 1] =
      static_cast<uint8_t>(authenticatedSec2Session >> 16);
  signedData[kDomainLength + 2] =
      static_cast<uint8_t>(authenticatedSec2Session >> 8);
  signedData[kDomainLength + 3] =
      static_cast<uint8_t>(authenticatedSec2Session);
  std::memcpy(signedData.data() + kDomainLength + 4,
              request + kPublicOffset, kPubLength + kNonceLength);
  std::array<uint8_t, 32> hash{};
  if (mbedtls_sha256(signedData.data(), signedData.size(),
                     hash.data(), 0) != 0) return false;

  mbedtls_ecdsa_context verifier;
  mbedtls_ecdsa_init(&verifier);
  bool valid =
      mbedtls_ecp_group_load(
          &verifier.MBEDTLS_PRIVATE(grp), MBEDTLS_ECP_DP_SECP256R1) == 0;
  if (valid)
    valid = mbedtls_ecp_point_read_binary(
        &verifier.MBEDTLS_PRIVATE(grp), &verifier.MBEDTLS_PRIVATE(Q),
        request + kPublicOffset, kPubLength) == 0;
  if (valid)
    valid = mbedtls_ecp_check_pubkey(
        &verifier.MBEDTLS_PRIVATE(grp), &verifier.MBEDTLS_PRIVATE(Q)) == 0;
  if (valid)
    valid = mbedtls_ecdsa_read_signature(
        &verifier, hash.data(), hash.size(),
        request + kHeaderLength, sigLength) == 0;
  mbedtls_ecdsa_free(&verifier);
  return valid;
}

}  // namespace config_v2_sec2_lab_peer_proof
#endif
