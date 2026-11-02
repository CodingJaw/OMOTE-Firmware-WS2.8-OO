#pragma once
// Host-testable conversion of a legacy device/action path to a *logical*
// public Config v2 path. Rejects absolute arbitrary paths and internal
// content-addressed object names, preventing fallback into legacy root.
// The snapshot resolver must perform the final manifest presence check.
#include "config_v2_path_policy.hpp"
#include <string>
#include <string_view>

namespace config_v2_logical_paths {

inline std::string inDirectory(std::string_view input,
                               std::string_view directory) {
  constexpr std::string_view kVfs = "/littlefs/";
  if (input.substr(0, kVfs.size()) == kVfs)
    input.remove_prefix(kVfs.size());
  if (input.size() <= directory.size() + 1 ||
      input.substr(0, directory.size()) != directory ||
      input[directory.size()] != '/' ||
      !config_v2_path_policy::isConfigSubdirectory(input) ||
      !config_v2_path_policy::publicReadable(input))
    return {};
  return std::string(input);
}

} // namespace config_v2_logical_paths
