#pragma once
// Exact-versioned public-key fingerprint pin for ONE HA instance.
// This is NOT a cryptographic session, PAKE, private key, or proof of pairing.
#include <cstddef>
#include <string>
#include <string_view>

namespace config_v2_pairing_trust_record {
constexpr std::size_t kFingerprintLength = 64;
constexpr char kPrefix[] = "omote-paired-ha-v1:";
inline bool validFingerprint(std::string_view s) {
  if (s.size() != kFingerprintLength) return false;
  for (char c : s)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return false;
  return true;
}
inline bool encode(std::string_view fingerprint, std::string &out) {
  out.clear();
  if (!validFingerprint(fingerprint)) return false;
  out.assign(kPrefix);
  out.append(fingerprint.data(), fingerprint.size());
  return true;
}
inline bool decode(std::string_view bytes, std::string &fingerprint) {
  fingerprint.clear();
  constexpr std::string_view prefix(kPrefix, sizeof(kPrefix) - 1);
  if (bytes.size() != prefix.size() + kFingerprintLength ||
      bytes.substr(0, prefix.size()) != prefix)
    return false;
  const auto candidate = bytes.substr(prefix.size());
  if (!validFingerprint(candidate)) return false;
  fingerprint.assign(candidate.data(), candidate.size());
  return true;
}
} // namespace config_v2_pairing_trust_record
