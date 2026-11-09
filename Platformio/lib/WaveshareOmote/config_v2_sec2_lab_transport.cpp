// A deliberately isolated ESP-IDF Security2 SRP6a/AES-GCM transport.
//
// This file is included in normal Waveshare builds to catch compiler/API
// mismatches, but its networking implementation is available ONLY when
// OMOTE_CONFIG_V2_SEC2_LAB_PROBE is explicitly defined in a separate lab
// environment. Only local Settings touchscreen taps may start it. The
// public Arduino WebServer remains read-only and on its original port 80.
//
// This is an encrypted-interoperability *probe*, not secure device pairing.
// Candidate HA and laboratory device P-256 key-possession proofs are
// unprivileged encrypted endpoints. The lab device key is persistent NVS
// SOFTWARE material, not a protected or trusted production identity. There
// is NO touchscreen peer approval, HA trust, or privileged endpoint. Do NOT enable
// the lab flag in production firmware.
#if defined(OMOTE_WAVESHARE28V2) && defined(OMOTE_CONFIG_V2_PREVIEW)
#include "config_v2_sec2_lab_transport.hpp"
#include <Arduino.h>  // millis(): monotonic lab-window expiry on the ESP32

#include <cstdlib>
#include <cstring>
#include <array>
#include <cstdio>
#include <string>

#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
#include "config_v2_sec2_lab_peer_proof.hpp"
#include <mbedtls/sha256.h>
#include "protocomm_httpd.h"
#include "protocomm_security2.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace config_v2_sec2_lab_transport {
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
Probe *Probe::activeLab_ = nullptr;

namespace {
// The bundled Arduino/ESP-IDF protocomm headers may omit patch_ver and
// protocomm_get_sec_version(). Never rely on a newer function that might
// not exist in the linked IDF binary. For a header without patch_ver, the
// implementation follows the legacy Security2 nonce convention (patch 0).
// A newer implementation exposes the actual value in its security object.
template <typename Security>
auto securityPatchVersion(const Security &security, int)
    -> decltype(static_cast<std::uint8_t>(security.patch_ver)) {
  return static_cast<std::uint8_t>(security.patch_ver);
}
template <typename Security>
std::uint8_t securityPatchVersion(const Security &, long) {
  return 0;
}

// Fingerprints are derived ONLY from a public key after its proof has been
// independently verified inside this same encrypted Security2 session.
// They are never obtained from untrusted public port-80 metadata.
std::string fingerprintOfVerifiedPublic(const std::uint8_t *publicKey65) {
  std::uint8_t hash[32] = {};
  if (!publicKey65 || mbedtls_sha256(publicKey65, 65, hash, 0) != 0)
    return {};
  constexpr char hex[] = "0123456789abcdef";
  std::string fingerprint(64, '0');
  for (std::size_t i = 0; i < 32; ++i) {
    fingerprint[2 * i] = hex[hash[i] >> 4];
    fingerprint[2 * i + 1] = hex[hash[i] & 0x0f];
  }
  return fingerprint;
}
} // namespace
#endif

Probe::~Probe() { stop(); }

#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
config_v2_sec2_lab_mutual_review::Snapshot Probe::labReviewSnapshot(
    std::uint32_t nowMs) {
  std::lock_guard<std::mutex> lock(reviewMutex_);
  return labReview_.snapshot(nowMs);
}

config_v2_sec2_lab_mutual_review::Result
Probe::acknowledgeLabReviewFromLocalTouch(std::uint32_t nowMs) {
  std::lock_guard<std::mutex> lock(reviewMutex_);
  if (!active() ||
      static_cast<std::uint32_t>(nowMs - startedAtMs_) >= kMaxWindowMs)
    return config_v2_sec2_lab_mutual_review::Result::NotReady;
  return labReview_.acknowledgeFromLocalTouch(nowMs);
}

void Probe::rejectLabReviewFromLocalTouch() {
  std::lock_guard<std::mutex> lock(reviewMutex_);
  labReview_.rejectFromLocalTouch();
}


esp_err_t Probe::gatedSecurityRequest(
    protocomm_security_handle_t handle, const void *params,
    std::uint32_t session, const std::uint8_t *input, ssize_t inLen,
    std::uint8_t **output, ssize_t *outLen, void *ctx) {
  Probe *lab = activeLab_;
  // This gate wraps the *real* Espressif Security2 request handler.
  // Every incoming Security2 frame (both the public-key and proof frames)
  // consumes its own persisted quota BEFORE any SRP work is performed.
  // This is intentionally stricter than five complete PIN attempts.
  // Never reset this NVS count from HTTPD, reboot, or a network endpoint.
  if (lab == nullptr || !lab->active() ||
      static_cast<std::uint32_t>(millis() - lab->startedAtMs_) >= kMaxWindowMs)
    return ESP_ERR_INVALID_STATE;
  constexpr std::uint32_t kMaximumMessages = 5;
  if (lab->messageStore_.reserveSecurityMessage(kMaximumMessages) !=
      config_v2_pairing_attempt_budget::Reservation::Allowed)
    return ESP_ERR_INVALID_STATE;
  if (!protocomm_security2.security_req_handler)
    return ESP_ERR_INVALID_STATE;
  return protocomm_security2.security_req_handler(
      handle, params, session, input, inLen, output, outLen, ctx);
}

esp_err_t Probe::probeRequest(std::uint32_t /*session*/,
                             const std::uint8_t *input, ssize_t inLen,
                             std::uint8_t **output, ssize_t *outLen,
                             void *ctx) {
  // Called only after protocomm has decrypted a completed Security2 session.
  // This endpoint returns no secrets, identity assertions, approvals or
  // access to filesystem/configuration operations.
  static constexpr char kRequest[] = "OMOTE-SEC2-PROBE-V1";
  static constexpr char kReply[] = "OMOTE-SEC2-OK-V1";
  if (ctx == nullptr || input == nullptr || output == nullptr ||
      outLen == nullptr || inLen != sizeof(kRequest) - 1 ||
      std::memcmp(input, kRequest, sizeof(kRequest) - 1) != 0)
    return ESP_ERR_INVALID_ARG;
  auto *self = static_cast<Probe *>(ctx);
  if (!self->active()) return ESP_ERR_INVALID_STATE;
  std::uint8_t *reply = static_cast<std::uint8_t *>(
      std::malloc(sizeof(kReply) - 1));
  if (!reply) return ESP_ERR_NO_MEM;
  std::memcpy(reply, kReply, sizeof(kReply) - 1);
  *output = reply;
  *outLen = sizeof(kReply) - 1;
  return ESP_OK;
}

esp_err_t Probe::deviceProofRequest(
    std::uint32_t session, const std::uint8_t *input, ssize_t inLen,
    std::uint8_t **output, ssize_t *outLen, void *ctx) {
  auto *self = static_cast<Probe *>(ctx);
  if (self == nullptr || !self->active() || !self->deviceIdentity_.ready() ||
      input == nullptr || output == nullptr || outLen == nullptr ||
      inLen != static_cast<ssize_t>(
          config_v2_sec2_lab_device_identity::kRequestBytes) ||
      static_cast<std::uint32_t>(millis() - self->startedAtMs_) >= kMaxWindowMs)
    return ESP_ERR_INVALID_STATE;
  // Called only AFTER AES-GCM decryption by protocomm. This signature
  // demonstrates possession of a laboratory NVS software key only. It
  // conveys NO trusted-device status or configuration privileges.
  std::array<std::uint8_t,
      config_v2_sec2_lab_device_identity::kReplyHeaderBytes +
      config_v2_sec2_lab_device_identity::kMaxSignatureBytes> buffer{};
  std::size_t bytes = 0;
  if (!self->deviceIdentity_.signSessionChallenge(
          session, input, static_cast<std::size_t>(inLen),
          buffer.data(), buffer.size(), bytes))
    return ESP_ERR_INVALID_ARG;
  auto *reply = static_cast<std::uint8_t *>(std::malloc(bytes));
  if (!reply) return ESP_ERR_NO_MEM;
  std::memcpy(reply, buffer.data(), bytes);
  const auto fp = fingerprintOfVerifiedPublic(buffer.data() + 1);
  config_v2_sec2_lab_mutual_review::Result reviewResult;
  {
    std::lock_guard<std::mutex> lock(self->reviewMutex_);
    reviewResult = self->labReview_.observeVerifiedDevice(session, fp, millis());
  }
  if (reviewResult != config_v2_sec2_lab_mutual_review::Result::Ok &&
      reviewResult != config_v2_sec2_lab_mutual_review::Result::TouchRequired) {
    std::free(reply);
    return ESP_ERR_INVALID_STATE;
  }
  *output = reply;
  *outLen = static_cast<ssize_t>(bytes);
  return ESP_OK;
}

esp_err_t Probe::candidateProofRequest(
    std::uint32_t session, const std::uint8_t *input, ssize_t inLen,
    std::uint8_t **output, ssize_t *outLen, void *ctx) {
  auto *self = static_cast<Probe *>(ctx);
  if (self == nullptr || !self->active() || input == nullptr ||
      inLen <= 0 || output == nullptr || outLen == nullptr ||
      static_cast<std::uint32_t>(millis() - self->startedAtMs_) >= kMaxWindowMs)
    return ESP_ERR_INVALID_STATE;
  // The protocomm endpoint decrypts via Security2 AES-GCM BEFORE calling
  // here. Proof is bound to the same session_id as its encryption context.
  if (!config_v2_sec2_lab_peer_proof::verifyCandidate(
          session, input, static_cast<std::size_t>(inLen)))
    return ESP_ERR_INVALID_ARG;
  // Only after actual P-256 signature verification has succeeded do we
  // permit this candidate to enter the volatile same-session lab review.
  const auto fp = fingerprintOfVerifiedPublic(input + 1);
  config_v2_sec2_lab_mutual_review::Result reviewResult;
  {
    std::lock_guard<std::mutex> lock(self->reviewMutex_);
    reviewResult = self->labReview_.observeVerifiedHa(session, fp, millis());
  }
  if (reviewResult != config_v2_sec2_lab_mutual_review::Result::Ok &&
      reviewResult != config_v2_sec2_lab_mutual_review::Result::TouchRequired)
    return ESP_ERR_INVALID_STATE;
  constexpr auto kBytes = sizeof(config_v2_sec2_lab_peer_proof::kResponse) - 1;
  auto *reply = static_cast<std::uint8_t *>(std::malloc(kBytes));
  if (!reply) return ESP_ERR_NO_MEM;
  std::memcpy(reply, config_v2_sec2_lab_peer_proof::kResponse, kBytes);
  *output = reply;
  *outLen = kBytes;
  // Deliberately NOT a pairing event: no peer is trusted, no NVS key is
  // stored, and no configuration mutation is available.
  return ESP_OK;
}

#if defined(OMOTE_CONFIG_V2_SEC2_TX_V2_LAB)
// Both callbacks execute only after protocomm has decrypted and authenticated
// the current Security2 session. They never return credentials or authorize
// configuration changes. The local transaction coordinator enforces matching
// session IDs, a fresh device-generated transaction and one-shot completion.
esp_err_t Probe::txV2OpenRequest(
    std::uint32_t session, const std::uint8_t *input, ssize_t inLen,
    std::uint8_t **output, ssize_t *outLen, void *ctx) {
  auto *self=static_cast<Probe *>(ctx);
  if (!self || !self->active() || !output || !outLen || !input ||
      inLen<=0 || static_cast<std::uint32_t>(
          millis()-self->startedAtMs_)>=kMaxWindowMs)
    return ESP_ERR_INVALID_STATE;
  std::vector<std::uint8_t> response;
  if (!self->txV2_.open(session,input,static_cast<std::size_t>(inLen),
                         millis(),self->deviceIdentity_,response))
    return ESP_ERR_INVALID_ARG;
  auto *buffer=static_cast<std::uint8_t *>(std::malloc(response.size()));
  if (!buffer) return ESP_ERR_NO_MEM;
  std::memcpy(buffer,response.data(),response.size());
  *output=buffer;
  *outLen=static_cast<ssize_t>(response.size());
  return ESP_OK;
}

esp_err_t Probe::txV2CommitRequest(
    std::uint32_t session, const std::uint8_t *input, ssize_t inLen,
    std::uint8_t **output, ssize_t *outLen, void *ctx) {
  auto *self=static_cast<Probe *>(ctx);
  if (!self || !self->active() || !output || !outLen || !input ||
      inLen<=0 || static_cast<std::uint32_t>(
          millis()-self->startedAtMs_)>=kMaxWindowMs)
    return ESP_ERR_INVALID_STATE;
  std::vector<std::uint8_t> response;
  config_v2_sec2_tx_v2_lab::VerifiedPublicKeys verified{};
  if (!self->txV2_.commit(session,input,static_cast<std::size_t>(inLen),
                           millis(),self->deviceIdentity_,response,verified) ||
      !verified.valid)
    return ESP_ERR_INVALID_ARG;
  // Only the successfully authenticated v2 transcript can present public
  // identity evidence to the local touchscreen. Never store it as trust.
  const auto haFp = fingerprintOfVerifiedPublic(verified.ha.data());
  const auto deviceFp = fingerprintOfVerifiedPublic(verified.device.data());
  if (haFp.empty() || deviceFp.empty()) return ESP_ERR_INVALID_STATE;
  auto *buffer=static_cast<std::uint8_t *>(std::malloc(response.size()));
  if (!buffer) return ESP_ERR_NO_MEM;
  std::memcpy(buffer,response.data(),response.size());
  // Both observations are bound to the EXACT callback session and are
  // protected from other endpoint callbacks by the existing review mutex.
  config_v2_sec2_lab_mutual_review::Result haReview;
  config_v2_sec2_lab_mutual_review::Result deviceReview;
  {
    std::lock_guard<std::mutex> lock(self->reviewMutex_);
    const auto now=millis();
    haReview=self->labReview_.observeVerifiedHa(session,haFp,now);
    deviceReview=haReview==config_v2_sec2_lab_mutual_review::Result::Ok
        ? self->labReview_.observeVerifiedDevice(session,deviceFp,now)
        : config_v2_sec2_lab_mutual_review::Result::NotReady;
  }
  if (haReview!=config_v2_sec2_lab_mutual_review::Result::Ok ||
      deviceReview!=config_v2_sec2_lab_mutual_review::Result::TouchRequired) {
    std::free(buffer);
    return ESP_ERR_INVALID_STATE;
  }
  *output=buffer;
  *outLen=static_cast<ssize_t>(response.size());
  return ESP_OK;
}
// Status poll has NO state-change operations: input is the digest of the
// fully verified live transaction, validated against the same decrypted
// protocomm session. Only physical touch can change review state.
// Request: 0x02 || transcript_digest[32].
// Response: 0x02 || (0x00 pending / 0x01 local-reviewed) || digest[32].
// A rejected, expired, closed or different-session review FAILS CLOSED.
esp_err_t Probe::txV2ReviewRequest(
    std::uint32_t session, const std::uint8_t *input, ssize_t inLen,
    std::uint8_t **output, ssize_t *outLen, void *ctx) {
  auto *self=static_cast<Probe *>(ctx);
  if (!self || !self->active() || !input || !output || !outLen ||
      inLen!=33 || input[0]!=config_v2_sec2_tx_v2_lab::kProtocolVersion ||
      static_cast<std::uint32_t>(millis()-self->startedAtMs_)>=kMaxWindowMs)
    return ESP_ERR_INVALID_STATE;
  if (!self->txV2_.matchesCompleted(session,input+1))
    return ESP_ERR_INVALID_ARG;
  config_v2_sec2_lab_mutual_review::Snapshot snapshot;
  {
    std::lock_guard<std::mutex> lock(self->reviewMutex_);
    snapshot=self->labReview_.snapshot(millis());
  }
  if (snapshot.verifiedSession!=session ||
      (snapshot.state!=config_v2_sec2_lab_mutual_review::State::AwaitingTouch &&
       snapshot.state!=config_v2_sec2_lab_mutual_review::State::Reviewed))
    return ESP_ERR_INVALID_STATE;
  std::uint8_t *buffer=static_cast<std::uint8_t *>(std::malloc(34));
  if (!buffer) return ESP_ERR_NO_MEM;
  buffer[0]=config_v2_sec2_tx_v2_lab::kProtocolVersion;
  buffer[1]=(snapshot.state==config_v2_sec2_lab_mutual_review::State::Reviewed)
      ? 0x01 : 0x00;
  std::memcpy(buffer+2,input+1,32);
  *output=buffer;
  *outLen=34;
  return ESP_OK;
}
// Additional LAB-only endpoint: issue one device-signed record of an
// ALREADY completed physical Review OK. No network request can approve touch.
// Request: 0x02 || transcript_sha256[32] || fresh_client_challenge[32].
// Reply: 0x02 || 0x01_local_reviewed || u8_der_len || P256_DER_signature.
// Signature domain binds version, Security2 session, transaction transcript,
// client challenge and reviewed state. This is NOT enrollment permission.
esp_err_t Probe::txV2TouchReceiptRequest(
    std::uint32_t session, const std::uint8_t *input, ssize_t inLen,
    std::uint8_t **output, ssize_t *outLen, void *ctx) {
  auto *self=static_cast<Probe *>(ctx);
  if (!self || !self->active() || !input || !output || !outLen ||
      inLen!=static_cast<ssize_t>(
          config_v2_sec2_tx_v2_lab::kTouchReceiptRequestBytes) ||
      input[0]!=config_v2_sec2_tx_v2_lab::kProtocolVersion ||
      static_cast<std::uint32_t>(millis()-self->startedAtMs_)>=kMaxWindowMs)
    return ESP_ERR_INVALID_STATE;
  // The one-shot signature is guarded by both mutexes and is impossible
  // unless the authenticated session's local touchscreen review was
  // explicitly accepted by a physical LVGL callback.
  auto *buffer=static_cast<std::uint8_t *>(std::malloc(3+80));
  if (!buffer) return ESP_ERR_NO_MEM;
  std::vector<std::uint8_t> receipt;
  bool allowed=false;
  {
    std::lock_guard<std::mutex> lock(self->reviewMutex_);
    const auto snapshot=self->labReview_.snapshot(millis());
    if (snapshot.verifiedSession==session &&
        snapshot.state==config_v2_sec2_lab_mutual_review::State::Reviewed) {
      allowed=self->txV2_.signLocalTouchReceipt(
          session,input+1,input+33,self->deviceIdentity_,receipt);
    }
  }
  if (!allowed || receipt.size()<3+8 || receipt.size()>3+80) {
    std::free(buffer);
    return ESP_ERR_INVALID_STATE;
  }
  std::memcpy(buffer,receipt.data(),receipt.size());
  *output=buffer;
  *outLen=static_cast<ssize_t>(receipt.size());
  return ESP_OK;
}
#endif

bool Probe::startFromTrustedLocalTouch(const char *pin,
                                      std::size_t len,
                                      std::uint32_t nowMs) {
  if (active() || activeLab_ != nullptr) return false;
  {
    std::lock_guard<std::mutex> lock(reviewMutex_);
    labReview_.beginFromLocalTouch(nowMs);
  }
  // The ONLY identity provisioning entrypoint is this physical touch path.
  // Never create or replace a device key from a network endpoint.
  if (!deviceIdentity_.prepareFromTrustedLocalTouch()) return false;
  if (!material_.generate(pin, len)) {
    deviceIdentity_.clear();
    return false;
  }
  protocomm_t *candidate = protocomm_new();
  if (!candidate) {
    material_.clear();
    deviceIdentity_.clear();
    return false;
  }

  protocomm_httpd_config_t config = {};
  config.ext_handle_provided = false;
  config.data.config.port = kPort;
  config.data.config.stack_size = 6144;
  config.data.config.task_priority = tskIDLE_PRIORITY + 5;
  // Publication precedes HTTPD registration, but an incomplete listener
  // cannot authenticate because active() is false until setup finishes.
  activeLab_ = this;
  startedAtMs_ = nowMs;
  boundedSecurity_ = protocomm_security2;
  boundedSecurity_.security_req_handler = &Probe::gatedSecurityRequest;
  bool started = false;
  bool good = protocomm_httpd_start(candidate, &config) == ESP_OK;
  if (good) {
    started = true;
    // protocomm borrows the params pointer until HTTPD is fully stopped.
    // Keep BOTH the struct and its salt/verifier allocations alive in Probe.
    securityParams_ = material_.params();
    good = protocomm_set_security(
        candidate, kSessionEndpoint, &boundedSecurity_, &securityParams_) == ESP_OK;
  }
  if (good) {
    char version[112] = {};
    // The installed protocomm component is already bound to
    // boundedSecurity_ above. Obtain the version from that actual object,
    // not from an optional/unavailable IDF helper API. The Python client
    // must use the same nonce-counter patch level (legacy 0, or patched 1).
    const int versionNumber = boundedSecurity_.ver;
    const std::uint8_t patchNumber = securityPatchVersion(boundedSecurity_, 0);
    const bool versionOk = versionNumber == 2 &&
                           (patchNumber == 0 || patchNumber == 1);
    const int n = versionOk ? std::snprintf(
        version, sizeof(version),
        "{\"prov\":{\"ver\":\"omote-sec2-lab-v1\",\"sec_patch_ver\":%u}}",
        static_cast<unsigned>(patchNumber)) : -1;
    good = versionOk && n > 0 &&
           static_cast<std::size_t>(n) < sizeof(version) &&
           protocomm_set_version(candidate, kVersionEndpoint, version) == ESP_OK;
  }
  if (good)
    good = protocomm_add_endpoint(
        candidate, kProbeEndpoint, probeRequest, this) == ESP_OK;
// ESP-IDF 5.3.2 protocomm_httpd_start() uses HTTPD_DEFAULT_CONFIG(), whose
// URI handler limit is EIGHT and cannot be overridden by the supplied
// protocomm_httpd_config_t. Security2 session and version take two slots.
// The v2 LAB needs probe + four v2 endpoints: seven total.
// Legacy standalone candidate/device proof endpoints are redundant in this
// dedicated signed v2 LAB. Retain both in the ORIGINAL sec2_lab environment.
#if !defined(OMOTE_CONFIG_V2_SEC2_TX_V2_LAB)
  if (good)
    good = protocomm_add_endpoint(
        candidate, kCandidateProofEndpoint, candidateProofRequest, this) == ESP_OK;
  if (good)
    good = protocomm_add_endpoint(
        candidate, kDeviceProofEndpoint, deviceProofRequest, this) == ESP_OK;
#endif
#if defined(OMOTE_CONFIG_V2_SEC2_TX_V2_LAB)
  if (good)
    good = protocomm_add_endpoint(
        candidate,kTxV2OpenEndpoint,txV2OpenRequest,this) == ESP_OK;
  if (good)
    good = protocomm_add_endpoint(
        candidate,kTxV2CommitEndpoint,txV2CommitRequest,this) == ESP_OK;
  if (good)
    good = protocomm_add_endpoint(
        candidate,kTxV2ReviewEndpoint,txV2ReviewRequest,this) == ESP_OK;
  if (good)
    good = protocomm_add_endpoint(
        candidate,kTxV2ReceiptEndpoint,txV2TouchReceiptRequest,this) == ESP_OK;
#endif

  if (!good) {
    // Never wipe salt/verifier while Security2 might still be using them.
    if (started && protocomm_httpd_stop(candidate) != ESP_OK) {
      // A failing HTTPD stop may leave callbacks borrowing the SRP buffers.
      // Fail shut by rebooting the LAB image rather than freeing them.
      // No service or pairing window auto-starts after reboot.
      esp_restart();
      return false;
    }
    protocomm_delete(candidate);
    activeLab_ = nullptr;
    material_.clear();
    deviceIdentity_.clear();
    return false;
  }
  pc_ = candidate;
  startedAtMs_ = nowMs;
  return true;
}

void Probe::tick(std::uint32_t nowMs) {
  if (active() &&
      static_cast<std::uint32_t>(nowMs - startedAtMs_) >= kMaxWindowMs)
    stop();
}

void Probe::stop() {
#if defined(OMOTE_CONFIG_V2_SEC2_TX_V2_LAB)
  txV2_.close();
#endif
  if (!pc_) {
    if (activeLab_ == this) activeLab_ = nullptr;
    {
      std::lock_guard<std::mutex> lock(reviewMutex_);
      labReview_.close();
    }
    material_.clear();
    deviceIdentity_.clear();
    return;
  }
  // Do not delete the Security2 instance or its borrowed SRP buffers until
  // HTTPD has fully stopped, so concurrent network callbacks cannot read
  // freed salt/verifier memory.
  if (protocomm_httpd_stop(pc_) != ESP_OK) {
    // A destructor would otherwise free borrowed Security2 parameters
    // while a still-running HTTPD task might dereference them. In the
    // explicit LAB environment, a restart is the safer failure mode.
    esp_restart();
    return;
  }
  protocomm_delete(pc_);
  pc_ = nullptr;
  if (activeLab_ == this) activeLab_ = nullptr;
  {
    std::lock_guard<std::mutex> lock(reviewMutex_);
    labReview_.close();
  }
  material_.clear();
  deviceIdentity_.clear();
}

#else  // Lab transport explicitly disabled in normal firmware

esp_err_t Probe::probeRequest(std::uint32_t, const std::uint8_t *,
                             ssize_t, std::uint8_t **, ssize_t *, void *) {
  return ESP_ERR_NOT_SUPPORTED;
}
bool Probe::startFromTrustedLocalTouch(
    const char *, std::size_t, std::uint32_t) {
  return false;
}
void Probe::tick(std::uint32_t) {}
void Probe::stop() { material_.clear(); }

#endif

} // namespace config_v2_sec2_lab_transport
#endif
