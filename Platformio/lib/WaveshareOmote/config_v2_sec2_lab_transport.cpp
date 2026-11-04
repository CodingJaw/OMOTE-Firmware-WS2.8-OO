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

#include <cstdlib>
#include <cstring>
#include <cstdio>

#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
#include "protocomm_httpd.h"
#include "protocomm_security2.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace config_v2_sec2_lab_transport {

Probe::~Probe() { stop(); }

#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)

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

bool Probe::startFromTrustedLocalTouch(const char *pin,
                                      std::size_t len,
                                      std::uint32_t nowMs) {
  if (active()) return false;
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
  bool started = false;
  bool good = protocomm_httpd_start(candidate, &config) == ESP_OK;
  if (good) {
    started = true;
    const auto params = material_.params();
    good = protocomm_set_security(
        candidate, kSessionEndpoint, &protocomm_security2, &params) == ESP_OK;
  }
  if (good) {
    char version[112] = {};
    const int n = std::snprintf(
        version, sizeof(version),
        "{\"prov\":{\"ver\":\"omote-sec2-lab-v1\",\"sec_patch_ver\":%u}}",
        static_cast<unsigned>(protocomm_security2.patch_ver));
    good = n > 0 && static_cast<std::size_t>(n) < sizeof(version) &&
           protocomm_set_version(candidate, kVersionEndpoint, version) == ESP_OK;
  }
  if (good)
    good = protocomm_add_endpoint(
        candidate, kProbeEndpoint, probeRequest, this) == ESP_OK;

  if (!good) {
    // Never wipe salt/verifier while Security2 might still be using them.
    if (started && protocomm_httpd_stop(candidate) != ESP_OK) {
      // HTTPD could still reference candidate; retain it and retry stopping
      // during tick. No success or paired state can be returned.
      pc_ = candidate;
      startedAtMs_ = nowMs - kMaxWindowMs;
      return false;
    }
    protocomm_delete(candidate);
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
    material_.clear();
    return;
  }
  // Do not delete the Security2 instance or its borrowed SRP buffers until
  // HTTPD has fully stopped, so concurrent network callbacks cannot read
  // freed salt/verifier memory.
  if (protocomm_httpd_stop(pc_) != ESP_OK) return;
  protocomm_delete(pc_);
  pc_ = nullptr;
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
