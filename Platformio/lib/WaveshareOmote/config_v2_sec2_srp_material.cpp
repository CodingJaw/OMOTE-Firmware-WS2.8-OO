// Device-side SRP-6a ephemeral material for a genuinely local PIN.
// No endpoint, network listener, pairing acceptance or device write here.
#if defined(OMOTE_WAVESHARE28V2) && defined(OMOTE_CONFIG_V2_PREVIEW)
#include "config_v2_sec2_srp_material.hpp"
#include "esp_srp.h"
#include <cstdlib>
#include <limits>

namespace config_v2_sec2_srp_material {
namespace {
void wipe(char *data, std::size_t length) {
  if (data == nullptr) return;
  volatile unsigned char *bytes =
      reinterpret_cast<volatile unsigned char *>(data);
  while (length--) *bytes++ = 0;
}
} // namespace

Ephemeral::~Ephemeral() { clear(); }

void Ephemeral::clear() {
  if (salt_ != nullptr) {
    wipe(salt_, static_cast<std::size_t>(kSaltBytes));
    std::free(salt_);
    salt_ = nullptr;
  }
  if (verifier_ != nullptr) {
    wipe(verifier_, verifierLength_);
    std::free(verifier_);
    verifier_ = nullptr;
  }
  verifierLength_ = 0;
}

bool Ephemeral::generate(const char *pin, std::size_t length) {
  clear();
  if (pin == nullptr || length != kPinDigits) return false;
  for (std::size_t i = 0; i < length; ++i)
    if (pin[i] < '0' || pin[i] > '9') return false;
  char *generatedSalt = nullptr;
  char *generatedVerifier = nullptr;
  int generatedVerifierLength = 0;
  const esp_err_t status = esp_srp_gen_salt_verifier(
      kUsername, sizeof(kUsername) - 1,
      pin, static_cast<int>(length),
      &generatedSalt, kSaltBytes,
      &generatedVerifier, &generatedVerifierLength);
  if (status != ESP_OK || generatedSalt == nullptr ||
      generatedVerifier == nullptr || generatedVerifierLength <= 0 ||
      generatedVerifierLength > std::numeric_limits<std::uint16_t>::max()) {
    // On error the external function does not guarantee initialized sizes.
    // Do not read potentially incomplete buffers; never expose any output.
    if (generatedSalt != nullptr) std::free(generatedSalt);
    if (generatedVerifier != nullptr) std::free(generatedVerifier);
    return false;
  }
  salt_ = generatedSalt;
  verifier_ = generatedVerifier;
  verifierLength_ = static_cast<std::uint16_t>(generatedVerifierLength);
  return true;
}

bool Ephemeral::ready() const {
  return salt_ != nullptr && verifier_ != nullptr && verifierLength_ != 0;
}

protocomm_security2_params_t Ephemeral::params() const {
  if (!ready()) return {nullptr, 0, nullptr, 0};
  return {salt_, static_cast<std::uint16_t>(kSaltBytes),
          verifier_, verifierLength_};
}

} // namespace config_v2_sec2_srp_material
#endif
