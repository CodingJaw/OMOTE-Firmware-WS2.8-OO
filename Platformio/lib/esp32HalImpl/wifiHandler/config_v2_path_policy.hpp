#pragma once

// Host-testable path policy for OMOTE config v2. This module deliberately has
// no Arduino, network, UI, or filesystem dependencies.
#include <string_view>

namespace config_v2_path_policy {

inline bool safeRelative(std::string_view path) {
  if (path.empty() || path.size() > 192 || path.front() == '/' || path.back() == '/')
    return false;
  for (unsigned char ch : path) {
    // Disallow escaping/alternate separators, URL ambiguity, and controls.
    if (ch < 0x20 || ch == 0x7f || ch == '\\' || ch == '%' || ch == ':')
      return false;
  }
  std::size_t cursor = 0;
  while (cursor < path.size()) {
    const std::size_t end = path.find('/', cursor);
    const auto part = path.substr(cursor, end == std::string_view::npos
                                            ? std::string_view::npos : end - cursor);
    if (part.empty() || part == "." || part == "..")
      return false;
    if (end == std::string_view::npos)
      break;
    cursor = end + 1;
  }
  return true;
}

inline bool jsonFile(std::string_view path) {
  return path.size() >= 5 && path.substr(path.size() - 5) == ".json";
}

inline bool isConfigSubdirectory(std::string_view path) {
  constexpr std::string_view prefixes[] = {
      "Scenes/", "Pages/", "Commands/", "Devices/", "Actions/"};
  for (const auto prefix : prefixes)
    if (path.substr(0, prefix.size()) == prefix && jsonFile(path))
      return true;
  return false;
}

// Public v1 compatibility: no secrets, state caches, or credential-bearing
// DeviceSettings.json. A v1 caller must not be able to read internal v2 data.
inline bool publicReadable(std::string_view path) {
  if (!safeRelative(path))
    return false;
  return path == "Scenes.json" || path == "DeviceSettings.schema.json" ||
         path == "logSettings.json" || isConfigSubdirectory(path);
}

// A future authenticated v2 deployment MAY accept these paths, but only after
// device-bound authentication, encrypted transport, preflight, and quotas.
// Wi-Fi, BLE pairing and v2 private keys are stored in NVS and intentionally
// excluded from shared configuration packages.
inline bool deployable(std::string_view path) {
  if (!safeRelative(path))
    return false;
  if (publicReadable(path))
    return true;
  return path == "DeviceSettings.json" || path == "HaSettings.json" ||
         path == "mqtt.json" || path == "ntp.json" || path == "ftp.json";
}

inline bool containsSecrets(std::string_view path) {
  return path == "DeviceSettings.json" || path == "HaSettings.json" ||
         path == "mqtt.json" || path == "ftp.json";
}

} // namespace config_v2_path_policy
