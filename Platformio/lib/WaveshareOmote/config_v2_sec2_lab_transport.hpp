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
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
#include "config_v2_sec2_lab_budget.hpp"
#include "config_v2_sec2_lab_device_identity.hpp"
#include "config_v2_sec2_lab_mutual_review.hpp"
#if defined(OMOTE_CONFIG_V2_SEC2_TX_V2_LAB)
#include "config_v2_sec2_tx_v2_lab.hpp"
#endif
#include <mutex>
#endif
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
constexpr char kCandidateProofEndpoint[] = "omote-ha-proof";
constexpr char kDeviceProofEndpoint[] = "omote-device-proof";
#if defined(OMOTE_CONFIG_V2_SEC2_TX_V2_LAB)
constexpr char kTxV2OpenEndpoint[] = "omote-tx2-open";
constexpr char kTxV2CommitEndpoint[] = "omote-tx2-commit";
#endif

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
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
  // Volatile LAB-only results from both independently verified proofs.
  // This is NOT paired/trusted state and cannot enable operations.
  config_v2_sec2_lab_mutual_review::Snapshot labReviewSnapshot(
      std::uint32_t nowMs);
  config_v2_sec2_lab_mutual_review::Result
      acknowledgeLabReviewFromLocalTouch(std::uint32_t nowMs);
  void rejectLabReviewFromLocalTouch();
#endif

private:
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
  static esp_err_t gatedSecurityRequest(
      protocomm_security_handle_t handle, const void *params,
      std::uint32_t session,
      const std::uint8_t *input, ssize_t inLen,
      std::uint8_t **output, ssize_t *outLen, void *ctx);
#endif
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
  static esp_err_t deviceProofRequest(
      std::uint32_t session, const std::uint8_t *input, ssize_t inLen,
      std::uint8_t **output, ssize_t *outLen, void *ctx);
#if defined(OMOTE_CONFIG_V2_SEC2_TX_V2_LAB)
  static esp_err_t txV2OpenRequest(
      std::uint32_t session, const std::uint8_t *input, ssize_t inLen,
      std::uint8_t **output, ssize_t *outLen, void *ctx);
  static esp_err_t txV2CommitRequest(
      std::uint32_t session, const std::uint8_t *input, ssize_t inLen,
      std::uint8_t **output, ssize_t *outLen, void *ctx);
#endif
  static esp_err_t candidateProofRequest(
      std::uint32_t session, const std::uint8_t *input, ssize_t inLen,
      std::uint8_t **output, ssize_t *outLen, void *ctx);
#endif
  static esp_err_t probeRequest(std::uint32_t session,
                          const std::uint8_t *input, ssize_t inLen,
                          std::uint8_t **output, ssize_t *outLen,
                          void *ctx);
  protocomm_t *pc_ = nullptr;
  config_v2_sec2_srp_material::Ephemeral material_;
  // protocomm_set_security borrows this whole struct for the duration of
  // the listener. It must never point into a function-local stack frame.
  protocomm_security2_params_t securityParams_{};
  std::uint32_t startedAtMs_ = 0;
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
  protocomm_security_t boundedSecurity_{};
  config_v2_sec2_lab_budget::Store messageStore_;
  config_v2_sec2_lab_device_identity::Identity deviceIdentity_;
  std::mutex reviewMutex_;
  config_v2_sec2_lab_mutual_review::Review labReview_;
#if defined(OMOTE_CONFIG_V2_SEC2_TX_V2_LAB)
  config_v2_sec2_tx_v2_lab::Coordinator txV2_;
#endif
  static Probe *activeLab_;
#endif
};

} // namespace config_v2_sec2_lab_transport
#endif
