// Opt-in, volatile-only two-step encrypted Security2 v2 transaction proof.
// The client must complete SRP6a/AES-GCM on a touch-opened laboratory listener.
// This module DOES NOT pair, approve touch/admin, change NVS, or authorize IO.
#if defined(OMOTE_WAVESHARE28V2) && defined(OMOTE_CONFIG_V2_PREVIEW) && \
    defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE) && \
    defined(OMOTE_CONFIG_V2_SEC2_TX_V2_LAB)
#include "config_v2_sec2_tx_v2_lab.hpp"
#include <esp_system.h>
#include <esp_random.h> // esp_fill_random() in ESP-IDF 5.3
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace config_v2_sec2_tx_v2_lab {
namespace {
constexpr char kHaDomain[] = "OMOTE-SEC2-TX-PROOF-HA-V2\n";
constexpr char kDeviceDomain[] = "OMOTE-SEC2-TX-PROOF-DEVICE-V2\n";
constexpr char kDigestDomain[] = "OMOTE-SEC2-TX-TRANSCRIPT-V2\n";

void u32(std::vector<std::uint8_t> &out,std::uint32_t n) {
  out.push_back(static_cast<std::uint8_t>(n >> 24));
  out.push_back(static_cast<std::uint8_t>(n >> 16));
  out.push_back(static_cast<std::uint8_t>(n >> 8));
  out.push_back(static_cast<std::uint8_t>(n));
}
void append(std::vector<std::uint8_t> &out,const std::uint8_t *p,std::size_t n) {
  out.insert(out.end(),p,p+n);
}
template <std::size_t N>
void append(std::vector<std::uint8_t> &out,
            const std::array<std::uint8_t,N> &arr) {
  append(out,arr.data(),arr.size());
}
void prefix(std::vector<std::uint8_t> &out,const char *domain,std::size_t n) {
  out.insert(out.end(),domain,domain+n);
}
bool hash(const std::vector<std::uint8_t> &bytes,
          std::array<std::uint8_t,32> &result) {
  return mbedtls_sha256(bytes.data(),bytes.size(),result.data(),0)==0;
}
bool validPublic(const std::uint8_t *point65) {
  if (!point65 || point65[0] != 0x04) return false;
  mbedtls_ecdsa_context verifier;
  mbedtls_ecdsa_init(&verifier);
  bool good=mbedtls_ecp_group_load(
      &verifier.MBEDTLS_PRIVATE(grp),MBEDTLS_ECP_DP_SECP256R1)==0;
  if (good) good=mbedtls_ecp_point_read_binary(
      &verifier.MBEDTLS_PRIVATE(grp),&verifier.MBEDTLS_PRIVATE(Q),
      point65,65)==0;
  if (good) good=mbedtls_ecp_check_pubkey(
      &verifier.MBEDTLS_PRIVATE(grp),&verifier.MBEDTLS_PRIVATE(Q))==0;
  mbedtls_ecdsa_free(&verifier);
  return good;
}
bool verifyHa(const std::uint8_t *point65,
              const std::vector<std::uint8_t> &context,
              const std::uint8_t *signature,std::size_t length) {
  if (!validPublic(point65) || !signature || length<8 || length>80)
    return false;
  std::vector<std::uint8_t> signedBytes;
  signedBytes.reserve(sizeof(kHaDomain)-1+context.size());
  prefix(signedBytes,kHaDomain,sizeof(kHaDomain)-1);
  append(signedBytes,context.data(),context.size());
  std::array<std::uint8_t,32> digest{};
  if (!hash(signedBytes,digest)) return false;
  mbedtls_ecdsa_context verifier;
  mbedtls_ecdsa_init(&verifier);
  bool valid=mbedtls_ecp_group_load(
      &verifier.MBEDTLS_PRIVATE(grp),MBEDTLS_ECP_DP_SECP256R1)==0;
  if (valid) valid=mbedtls_ecp_point_read_binary(
      &verifier.MBEDTLS_PRIVATE(grp),&verifier.MBEDTLS_PRIVATE(Q),
      point65,65)==0;
  if (valid) valid=mbedtls_ecp_check_pubkey(
      &verifier.MBEDTLS_PRIVATE(grp),&verifier.MBEDTLS_PRIVATE(Q))==0;
  if (valid) valid=mbedtls_ecdsa_read_signature(
      &verifier,digest.data(),digest.size(),signature,length)==0;
  mbedtls_ecdsa_free(&verifier);
  return valid;
}
} // namespace

bool Coordinator::open(
    std::uint32_t session,const std::uint8_t *request,
    std::size_t requestLength,std::uint32_t nowMs,
    const config_v2_sec2_lab_device_identity::Identity &identity,
    std::vector<std::uint8_t> &reply) {
  reply.clear();
  if (!session || !request || requestLength!=kOpenRequestBytes ||
      request[0]!=kProtocolVersion || !identity.ready() ||
      !validPublic(request+1)) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  if (pending_.active &&
      static_cast<std::uint32_t>(nowMs-pending_.created)>=kTransactionTimeoutMs)
    pending_=Pending{};
  if (pending_.active || session==completedSession_) return false;

  Pending candidate{};
  candidate.session=session;
  candidate.created=nowMs;
  std::memcpy(candidate.haPub.data(),request+1,65);
  std::memcpy(candidate.haNonce.data(),request+1+65,32);
  if (!identity.copyLabPublicKey(candidate.devicePub.data(),65)) return false;
  std::array<std::uint8_t,32> fingerprint{};
  if (mbedtls_sha256(candidate.devicePub.data(),65,fingerprint.data(),0)!=0)
    return false;
  std::memcpy(candidate.deviceId.data(),fingerprint.data(),8);
  // Fresh remote-originating transaction ID and challenge; no network
  // endpoint accepts either as an authoritative value from the client.
  esp_fill_random(candidate.tx.data(),candidate.tx.size());
  esp_fill_random(candidate.deviceNonce.data(),candidate.deviceNonce.size());
  reply.reserve(kOpenReplyBytes);
  reply.push_back(kProtocolVersion);
  u32(reply,session);
  u32(reply,kLabEpoch);
  append(reply,candidate.tx);
  reply.push_back(static_cast<std::uint8_t>(candidate.deviceId.size()));
  append(reply,candidate.deviceId);
  append(reply,candidate.devicePub);
  append(reply,candidate.deviceNonce);
  if (reply.size()!=kOpenReplyBytes) {
    reply.clear();
    return false;
  }
  candidate.active=true;
  pending_=candidate;
  return true;
}

bool Coordinator::commit(
    std::uint32_t session,const std::uint8_t *request,
    std::size_t requestLength,std::uint32_t nowMs,
    const config_v2_sec2_lab_device_identity::Identity &identity,
    std::vector<std::uint8_t> &reply, VerifiedPublicKeys &verified) {
  reply.clear();
  verified = VerifiedPublicKeys{}; // Invalid on every failure path.
  if (!session || !request || requestLength<1+1+8 ||
      requestLength>kMaxCommitRequestBytes || request[0]!=kProtocolVersion ||
      requestLength!=static_cast<std::size_t>(2+request[1]) ||
      request[1]<8 || request[1]>80 || !identity.ready()) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!pending_.active || pending_.session!=session ||
      session==completedSession_) return false;
  if (static_cast<std::uint32_t>(nowMs-pending_.created)>=kTransactionTimeoutMs) {
    pending_=Pending{};
    return false;
  }
  // Fail closed on ANY invalid proof: a new locally initiated transaction
  // is required. This is not a production credential retry mechanism.
  const Pending p=pending_;
  pending_=Pending{};
  std::vector<std::uint8_t> context;
  context.reserve(228);
  context.push_back(kProtocolVersion);
  u32(context,session);
  u32(context,kLabEpoch);
  append(context,p.tx);
  context.push_back(static_cast<std::uint8_t>(p.deviceId.size()));
  append(context,p.deviceId);
  append(context,p.haPub);
  append(context,p.devicePub);
  append(context,p.haNonce);
  append(context,p.deviceNonce);
  if (context.size()!=228 ||
      !verifyHa(p.haPub.data(),context,request+2,request[1]))
    return false;
  std::vector<std::uint8_t> signedDevice;
  signedDevice.reserve(sizeof(kDeviceDomain)-1+context.size());
  prefix(signedDevice,kDeviceDomain,sizeof(kDeviceDomain)-1);
  append(signedDevice,context.data(),context.size());
  std::array<std::uint8_t,32> deviceDigest{};
  if (!hash(signedDevice,deviceDigest)) return false;
  std::array<std::uint8_t,80> signature{};
  std::size_t sigLength=0;
  if (!identity.signVerifiedLabDigest(deviceDigest.data(),
       signature.data(),signature.size(),sigLength)) return false;
  std::vector<std::uint8_t> transcript;
  transcript.reserve(sizeof(kDigestDomain)-1+context.size()+2+80+80);
  prefix(transcript,kDigestDomain,sizeof(kDigestDomain)-1);
  append(transcript,context.data(),context.size());
  transcript.push_back(request[1]);
  append(transcript,request+2,request[1]);
  transcript.push_back(static_cast<std::uint8_t>(sigLength));
  append(transcript,signature.data(),sigLength);
  std::array<std::uint8_t,32> transcriptDigest{};
  if (!hash(transcript,transcriptDigest)) return false;
  reply.reserve(2+sigLength+32);
  reply.push_back(kProtocolVersion);
  reply.push_back(static_cast<std::uint8_t>(sigLength));
  append(reply,signature.data(),sigLength);
  append(reply,transcriptDigest);
  // Publish public evidence only after HA verification, device signing and
  // the shared transcript hash all succeeded. No private key is copied.
  verified.ha=p.haPub;
  verified.device=p.devicePub;
  verified.valid=true;
  // Mark as consumed after successfully computing one non-authorizing proof.
  completedSession_=session;
  return true;
}

void Coordinator::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  pending_=Pending{};
  completedSession_=0;
}
} // namespace config_v2_sec2_tx_v2_lab
#endif
