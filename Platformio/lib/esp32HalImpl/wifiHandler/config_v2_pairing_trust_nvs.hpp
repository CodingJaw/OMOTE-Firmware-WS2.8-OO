#pragma once
// NVS HA fingerprint pin, isolated from the single-active config pointer.
// ESP32 private key material must be kept separately and protected.
#include <string>
namespace config_v2_pairing_trust_nvs {
enum class ReadStatus { Unpaired, Paired, Corrupt, IoError };
class Store {
public:
  ReadStatus read(std::string &fingerprint);
  // Internal trusted coordinator ONLY. Caller must have cryptographically
  // proven peer and one-shot on-device approval. This method proves neither.
  bool replaceAfterAuthenticatedPhysicalApproval(const std::string &fingerprint);
};
} // namespace config_v2_pairing_trust_nvs
