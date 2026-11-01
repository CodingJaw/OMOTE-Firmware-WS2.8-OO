#pragma once
// Shared lowercase SHA-256 identifier syntax for single-active configuration.
// No A/B selection, trial boot, rollback state or device access.
#include <string_view>

namespace config_v2_digest {

inline bool valid(std::string_view digest) {
  if (digest.size() != 64)
    return false;
  for (char c : digest)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return false;
  return true;
}

} // namespace config_v2_digest
