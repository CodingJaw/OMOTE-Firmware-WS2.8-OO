#pragma once
// LAB ONLY: proves possession of a *candidate* Home Assistant P-256 key.
// It DOES NOT authenticate device identity, authorize commands, or persist
// trusted peers. Only called by a post-Security2 encrypted protocomm endpoint.
//
// Request (all binary, big-endian):
//   version 0x01 (1 byte), SEC1 uncompressed P-256 pubkey (65 bytes),
//   random client nonce (32 bytes), signature length (u16, 2 bytes),
//   ASN.1 DER ECDSA-SHA256 signature (8..80 bytes).
// Signed message = ASCII domain (no NUL) || session_id (u32 BE)
//                  || exact 65-byte public key || exact 32-byte nonce.
// Both sides MUST keep this shape unchanged or bump the version.
#if defined(OMOTE_WAVESHARE28V2) && \
    defined(OMOTE_CONFIG_V2_PREVIEW) && \
    defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
#include <cstddef>
#include <cstdint>
namespace config_v2_sec2_lab_peer_proof {
constexpr char kDomain[] = "OMOTE-SEC2-LAB-HA-PROOF-V1";
constexpr char kResponse[] = "OMOTE-LAB-HA-PROOF-OK-V1";
constexpr std::size_t kPubLength = 65;
constexpr std::size_t kNonceLength = 32;
constexpr std::size_t kHeaderLength = 1 + kPubLength + kNonceLength + 2;
constexpr std::size_t kMaxSignatureLength = 80;

// This function is only a cryptographic proof of the supplied public key;
// the caller must NEVER treat success as a paired or trusted HA identity.
bool verifyCandidate(uint32_t authenticatedSec2Session,
                     const uint8_t *request, size_t requestLength);
}  // namespace config_v2_sec2_lab_peer_proof
#endif
