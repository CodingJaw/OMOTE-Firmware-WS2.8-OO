// A deliberately isolated ESP-IDF Security2 SRP6a/AES-GCM transport.
//
// This file is included in normal Waveshare builds to catch compiler/API
// mismatches, but its networking implementation is available ONLY when
// OMOTE_CONFIG_V2_SEC2_LAB_PROBE is explicitly defined in a separate lab
// environment. Nothing in OMOTE calls startFromTrustedLocalTouch(). The
// public Arduino WebServer remains read-only and on its original port 80.
//
// This is an encrypted-interoperability *probe*, not secure device pairing.
// There is no device/HA key-possession proof, persisted rate limit, UI
// confirmation, credential storage or privileged endpoint. Do NOT enable
// the lab flag in production firmware.
#if defined(OMOTE_WAVESHARE28V2) && defined(OMOTE_CONFIG_V2_PREVIEW)
#include "config_v2_sec2_lab_transport.hpp"
#include <Arduino.h>  // millis(): monotonic lab-window expiry on the ESP32

#include <cstdlib>
#include <cstring>
#include <cstdio>

#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
#include "config_v2_sec2_lab_peer_proof.hpp"
#include "protocomm_httpd.h"
#include "protocomm_security2.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace config_v2_sec2_lab_transport {
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
Probe *Probe::activeLab_ = nullptr;
#endif

Probe::~Probe() { stop(); }

#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)

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

bool Probe::startFromTrustedLocalTouch(const char *pin,
                                      std::size_t len,
                                      std::uint32_t nowMs) {
  if (active() || activeLab_ != nullptr) return false;
  if (!material_.generate(pin, len)) return false;
  protocomm_t *candidate = protocomm_new();
  if (!candidate) {
    material_.clear();
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
    const auto params = material_.params();
    good = protocomm_set_security(
        candidate, kSessionEndpoint, &boundedSecurity_, &params) == ESP_OK;
  }
  if (good) {
    char version[112] = {};
    // Probe the *effective* registered security version rather than
    // assuming the build's esp-idf struct exposes a patch_ver field.
    // Refuse older patch levels: the Python AES-GCM reference client
    // requires the v1 monotonic nonce-counter update.
    int versionNumber = 0;
    std::uint8_t patchNumber = 0;
    const bool versionOk =
        protocomm_get_sec_version(candidate, &versionNumber, &patchNumber) == ESP_OK &&
        versionNumber == 2 && patchNumber == 1;
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
  if (good)
    good = protocomm_add_endpoint(
        candidate, kCandidateProofEndpoint, candidateProofRequest, this) == ESP_OK;

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
  if (!pc_) {
    if (activeLab_ == this) activeLab_ = nullptr;
    material_.clear();
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
  material_.clear();
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
