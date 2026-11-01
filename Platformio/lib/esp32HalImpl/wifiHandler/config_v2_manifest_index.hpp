#pragma once

// Strict structural checks before parsing configuration JSON and checking its
// cross-file dependencies. This is NOT the complete deployment validator.
// No filesystem, network or Arduino dependencies; only explicit allowlisted
// configuration files can enter an immutable generation index.
#include "config_v2_path_policy.hpp"
#include "config_v2_digest.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace config_v2_manifest_index {

constexpr std::size_t kMaxEntries = 128;
constexpr uint64_t kMaxFileBytes = 256u * 1024u;
constexpr uint64_t kMaxTotalBytes = 2u * 1024u * 1024u;

struct Entry {
  std::string path;
  std::string sha256;
  uint64_t length = 0;
};

enum class Error {
  None,
  NoFiles,
  TooManyFiles,
  ForbiddenPath,
  DuplicatePath,
  InvalidHash,
  InvalidSize,
  QuotaExceeded,
  MissingSceneRegistry
};

struct Result {
  Error error = Error::None;
  uint64_t totalBytes = 0;
  std::size_t secretFiles = 0;
  bool ok() const { return error == Error::None; }
};

inline Result validate(const std::vector<Entry> &entries) {
  Result result;
  if (entries.empty()) {
    result.error = Error::NoFiles;
    return result;
  }
  if (entries.size() > kMaxEntries) {
    result.error = Error::TooManyFiles;
    return result;
  }
  std::unordered_set<std::string> names;
  bool hasScenes = false;
  for (const auto &entry : entries) {
    if (!config_v2_path_policy::deployable(entry.path)) {
      result.error = Error::ForbiddenPath;
      return result;
    }
    if (!names.insert(entry.path).second) {
      result.error = Error::DuplicatePath;
      return result;
    }
    if (!config_v2_digest::valid(entry.sha256)) {
      result.error = Error::InvalidHash;
      return result;
    }
    if (entry.length == 0 || entry.length > kMaxFileBytes) {
      result.error = Error::InvalidSize;
      return result;
    }
    // Subtraction avoids integer overflow on untrusted length values.
    if (result.totalBytes > kMaxTotalBytes - entry.length) {
      result.error = Error::QuotaExceeded;
      return result;
    }
    result.totalBytes += entry.length;
    if (config_v2_path_policy::containsSecrets(entry.path))
      ++result.secretFiles;
    if (entry.path == "Scenes.json")
      hasScenes = true;
  }
  if (!hasScenes)
    result.error = Error::MissingSceneRegistry;
  return result;
}

} // namespace config_v2_manifest_index
