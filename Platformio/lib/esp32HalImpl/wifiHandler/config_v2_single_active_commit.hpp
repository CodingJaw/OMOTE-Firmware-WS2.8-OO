#pragma once
// Internal, INERT single-active storage primitives. They are NOT exposed
// by HTTP or touchscreen UI. An audited authenticated manager must enforce
// fresh HA-admin AND physical approval before it may call activatePrepared.
// The preview build deliberately has no caller for activatePrepared.
#include "config_v2_manifest_index.hpp"
#include <string>
#include <vector>

namespace config_v2_single_active_commit {
enum class Status {
  Ok, Invalid, Conflict, MissingObject, GraphInvalid, NoSpace,
  CorruptManifest, IoError
};
struct Prepared {
  std::string revision; // SHA-256 of canonical manifest
  unsigned files = 0;
};
// Publish verified immutable objects/manifest but NEVER select or activate.
Status prepare(const std::vector<config_v2_manifest_index::Entry> &files,
               Prepared &out);
// Caller MUST run on a non-LVGL task after a cryptographically authenticated
// device-specific session and consume a fresh local touch approval. Requires
// the complete proposal to have been prepared and re-verified. Not wired up.
Status activatePrepared(const Prepared &prepared,
                        const std::string &expectedActive);
} // namespace config_v2_single_active_commit
