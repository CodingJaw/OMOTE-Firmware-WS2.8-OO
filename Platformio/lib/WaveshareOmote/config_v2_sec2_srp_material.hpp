#pragma once
// Real ESP-IDF SRP6a salt/verifier generation for a *local* six-digit PIN.
//
// Experimental only. This does not configure HTTPD, establish a Security2
// session, prove device/HA key identity, or authorize pairing/deployment.
// Caller must generate PIN via on-device CSPRNG and physical pairing window,
// enforce persistent attempts, and keep material alive while Security2 uses
// params(). Do not send the PIN over legacy port 80.
#if defined(OMOTE_WAVESHARE28V2) && defined(OMOTE_CONFIG_V2_PREVIEW)

#include "sdkconfig.h"
#include "protocomm_security.h"
#include <cstddef>
#include <cstdint>

namespace config_v2_sec2_srp_material {

constexpr std::size_t kPinDigits = 6;
constexpr int kSaltBytes = 16;
constexpr char kUsername[] = "omote-ha";

class Ephemeral {
public:
  Ephemeral() = default;
  Ephemeral(const Ephemeral &) = delete;
  Ephemeral &operator=(const Ephemeral &) = delete;
  ~Ephemeral();

  // PIN may be taken ONLY from the local touchscreen pairing coordinator.
  // The caller owns and must erase its separate PIN buffer.
  // False means no material may be used and the provisional channel must fail.
  bool generate(const char *pin, std::size_t length);
  void clear();
  bool ready() const;
  protocomm_security2_params_t params() const;

private:
  char *salt_ = nullptr;
  char *verifier_ = nullptr;
  std::uint16_t verifierLength_ = 0;
};

} // namespace config_v2_sec2_srp_material
#endif
