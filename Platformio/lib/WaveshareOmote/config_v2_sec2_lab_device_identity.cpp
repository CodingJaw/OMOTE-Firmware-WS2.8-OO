// LAB-only NVS-backed software device key and encrypted P-256 challenge proof.
// This key is NOT hardware protected without NVS encryption; never trust it
// for production pairing or grant privileged access because it signs a nonce.
#if defined(OMOTE_WAVESHARE28V2) && \
    defined(OMOTE_CONFIG_V2_PREVIEW) && \
    defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
#include "config_v2_sec2_lab_device_identity.hpp"
#include <Preferences.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/sha256.h>
#include <array>
#include <cstring>

namespace config_v2_sec2_lab_device_identity {
namespace {
constexpr char kNamespace[] = "omsec2id";
constexpr char kRecordKey[] = "labkey";
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kRecordLength = 1 + kPrivateBytes + kPublicBytes;
constexpr char kPersonalization[] = "omote-sec2-device-lab-v1";

void wipe(void *p, std::size_t n) {
  volatile std::uint8_t *bytes = static_cast<volatile std::uint8_t *>(p);
  while (n--) *bytes++ = 0;
}

struct SecretRecord {
  std::array<std::uint8_t, kRecordLength> bytes{};
  ~SecretRecord() { wipe(bytes.data(), bytes.size()); }
};

class Crypto {
public:
  Crypto() {
    mbedtls_ecdsa_init(&key);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&rng);
  }
  ~Crypto() {
    mbedtls_ecdsa_free(&key);
    mbedtls_ctr_drbg_free(&rng);
    mbedtls_entropy_free(&entropy);
  }
  Crypto(const Crypto &) = delete;
  Crypto &operator=(const Crypto &) = delete;
  bool seed() {
    return mbedtls_ctr_drbg_seed(
        &rng, mbedtls_entropy_func, &entropy,
        reinterpret_cast<const unsigned char *>(kPersonalization),
        sizeof(kPersonalization) - 1) == 0;
  }
  mbedtls_ecdsa_context key;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context rng;
};

bool exportPublic(Crypto &crypto, std::uint8_t *publicOut) {
  std::size_t size = 0;
  return mbedtls_ecp_point_write_binary(
             &crypto.key.MBEDTLS_PRIVATE(grp),
             &crypto.key.MBEDTLS_PRIVATE(Q),
             MBEDTLS_ECP_PF_UNCOMPRESSED, &size,
             publicOut, kPublicBytes) == 0 &&
         size == kPublicBytes && publicOut[0] == 0x04;
}

bool generate(Crypto &crypto, SecretRecord &record) {
  if (!crypto.seed() ||
      mbedtls_ecdsa_genkey(&crypto.key, MBEDTLS_ECP_DP_SECP256R1,
                           mbedtls_ctr_drbg_random, &crypto.rng) != 0)
    return false;
  record.bytes[0] = kVersion;
  return mbedtls_mpi_write_binary(
             &crypto.key.MBEDTLS_PRIVATE(d),
             record.bytes.data() + 1, kPrivateBytes) == 0 &&
         exportPublic(crypto, record.bytes.data() + 1 + kPrivateBytes);
}

bool validate(Crypto &crypto, const SecretRecord &record) {
  if (record.bytes[0] != kVersion ||
      record.bytes[1 + kPrivateBytes] != 0x04 ||
      !crypto.seed() ||
      mbedtls_ecp_group_load(&crypto.key.MBEDTLS_PRIVATE(grp),
                             MBEDTLS_ECP_DP_SECP256R1) != 0 ||
      mbedtls_mpi_read_binary(
          &crypto.key.MBEDTLS_PRIVATE(d),
          record.bytes.data() + 1, kPrivateBytes) != 0 ||
      mbedtls_ecp_check_privkey(
          &crypto.key.MBEDTLS_PRIVATE(grp),
          &crypto.key.MBEDTLS_PRIVATE(d)) != 0 ||
      mbedtls_ecp_mul(
          &crypto.key.MBEDTLS_PRIVATE(grp),
          &crypto.key.MBEDTLS_PRIVATE(Q),
          &crypto.key.MBEDTLS_PRIVATE(d),
          &crypto.key.MBEDTLS_PRIVATE(grp).G,
          mbedtls_ctr_drbg_random, &crypto.rng) != 0)
    return false;

  std::array<std::uint8_t, kPublicBytes> derived{};
  if (!exportPublic(crypto, derived.data())) return false;
  // A malformed, wrong-version or internally inconsistent identity is
  // a hard error. Never silently replace a device key after a flash fault.
  std::uint8_t diff = 0;
  for (std::size_t i = 0; i < kPublicBytes; ++i)
    diff |= derived[i] ^ record.bytes[1 + kPrivateBytes + i];
  return diff == 0;
}
} // namespace

void Identity::clear() {
  ready_ = false;
  wipe(private_, sizeof(private_));
  wipe(public_, sizeof(public_));
}

bool Identity::prepareFromTrustedLocalTouch() {
  clear();
  Preferences storage;
  if (!storage.begin(kNamespace, false)) return false;
  SecretRecord record;
  const bool present = storage.isKey(kRecordKey);
  bool good = false;
  if (present) {
    good = storage.getBytesLength(kRecordKey) == kRecordLength &&
           storage.getBytes(kRecordKey, record.bytes.data(),
                            record.bytes.size()) == kRecordLength;
    if (good) {
      Crypto verifier;
      good = validate(verifier, record);
    }
  } else {
    Crypto generator;
    good = generate(generator, record);
    if (good) {
      // Store a single versioned record atomically. Read it back and
      // validate before ever signing: partial or corrupt NVS fails closed.
      good = storage.putBytes(kRecordKey, record.bytes.data(),
                              record.bytes.size()) == kRecordLength;
      SecretRecord check;
      good = good && storage.getBytesLength(kRecordKey) == kRecordLength &&
             storage.getBytes(kRecordKey, check.bytes.data(),
                              check.bytes.size()) == kRecordLength;
      if (good) {
        std::uint8_t diff = 0;
        for (std::size_t i = 0; i < kRecordLength; ++i)
          diff |= record.bytes[i] ^ check.bytes[i];
        Crypto verifier;
        good = diff == 0 && validate(verifier, check);
      }
    }
  }
  storage.end();
  if (!good) return false;

  std::memcpy(private_, record.bytes.data() + 1, kPrivateBytes);
  std::memcpy(public_, record.bytes.data() + 1 + kPrivateBytes, kPublicBytes);
  ready_ = true;
  return true;
}

bool Identity::signSessionChallenge(
    std::uint32_t sessionId, const std::uint8_t *request,
    std::size_t requestLength, std::uint8_t *reply,
    std::size_t replyCapacity, std::size_t &replyLength) const {
  replyLength = 0;
  if (!ready_ || request == nullptr || reply == nullptr ||
      requestLength != kRequestBytes || request[0] != kVersion ||
      replyCapacity < kReplyHeaderBytes + kMaxSignatureBytes)
    return false;

  Crypto crypto;
  SecretRecord temporary;
  temporary.bytes[0] = kVersion;
  std::memcpy(temporary.bytes.data() + 1, private_, kPrivateBytes);
  std::memcpy(temporary.bytes.data() + 1 + kPrivateBytes,
              public_, kPublicBytes);
  if (!validate(crypto, temporary)) return false;

  // The session identifier is the same authenticated Security2 cookie value
  // provided to protocomm's encrypted endpoint callback.
  constexpr std::size_t domainLen = sizeof(kDomain) - 1;
  std::array<std::uint8_t,
             domainLen + 4 + kChallengeBytes + kPublicBytes> transcript{};
  std::memcpy(transcript.data(), kDomain, domainLen);
  transcript[domainLen] = static_cast<std::uint8_t>(sessionId >> 24);
  transcript[domainLen + 1] = static_cast<std::uint8_t>(sessionId >> 16);
  transcript[domainLen + 2] = static_cast<std::uint8_t>(sessionId >> 8);
  transcript[domainLen + 3] = static_cast<std::uint8_t>(sessionId);
  std::memcpy(transcript.data() + domainLen + 4,
              request + 1, kChallengeBytes);
  std::memcpy(transcript.data() + domainLen + 4 + kChallengeBytes,
              public_, kPublicBytes);

  std::array<std::uint8_t, 32> hash{};
  if (mbedtls_sha256(transcript.data(), transcript.size(),
                     hash.data(), 0) != 0)
    return false;

  reply[0] = kVersion;
  std::memcpy(reply + 1, public_, kPublicBytes);
  std::size_t signatureLength = 0;
  if (mbedtls_ecdsa_write_signature(
          &crypto.key, MBEDTLS_MD_SHA256, hash.data(), hash.size(),
          reply + kReplyHeaderBytes, kMaxSignatureBytes, &signatureLength,
          mbedtls_ctr_drbg_random, &crypto.rng) != 0 ||
      signatureLength < 8 || signatureLength > kMaxSignatureBytes)
    return false;
  reply[1 + kPublicBytes] =
      static_cast<std::uint8_t>(signatureLength >> 8);
  reply[1 + kPublicBytes + 1] =
      static_cast<std::uint8_t>(signatureLength);
  replyLength = kReplyHeaderBytes + signatureLength;
  return true;
}

} // namespace config_v2_sec2_lab_device_identity
#endif
