#pragma once
// Isolated Security2 interoperability PROBE, never a production pairing server.
//
// Only compiled on Waveshare Config-v2 preview. The usable listener is
// additionally guarded by OMOTE_CONFIG_V2_SEC2_LAB_PROBE. Only the LAB
// Settings LVGL physical-touch callback can start it; nothing starts on boot,
// HTTP, MQTT, or in ordinary firmware builds.
//
// LAB USE ONLY: A six-digit PIN is too weak without persistent per-attempt
// limits. The transport MUST NOT be activated in production until persistent
// attempts, peer-key proofs, trusted touchscreen approval and session
// cancellation are integrated. A completed probe is NOT a trusted pairing.
#if defined(OMOTE_WAVESHARE28V2) && defined(OMOTE_CONFIG_V2_PREVIEW)
#include "config_v2_sec2_srp_material.hpp"
#include "protocomm.h"
#include "esp_err.h"
#include <sys/types.h>
#include <cstddef>
#include <cstdint>

namespace config_v2_sec2_lab_transport {
constexpr std::uint16_t kPort = 18082;
constexpr std::uint32_t kMaxWindowMs = 120000;
constexpr char kSessionEndpoint[] = "prov-session";
constexpr char kVersionEndpoint[] = "proto-ver";
constexpr char kProbeEndpoint[] = "omote-probe";

class Probe {
public:
  Probe() = default;
  ~Probe();
  Probe(const Probe &) = delete;
  Probe &operator=(const Probe &) = delete;

  // Lab build + explicit local-touch action ONLY. Not wired in the app.
  // Must never be exposed to a remote command or public HTTP request.
  bool startFromTrustedLocalTouch(const char *pin, std::size_t len,
                                  std::uint32_t nowMs);
  void tick(std::uint32_t nowMs);
  void stop();
  bool active() const { return pc_ != nullptr; }

private:
  static esp_err_t probeRequest(std::uint32_t session,
                          const std::uint8_t *input, ssize_t inLen,
                          std::uint8_t **output, ssize_t *outLen,
                          void *ctx);
  protocomm_t *pc_ = nullptr;
  config_v2_sec2_srp_material::Ephemeral material_;
  std::uint32_t startedAtMs_ = 0;
};

} // namespace config_v2_sec2_lab_transport
#endif
