#if defined(OMOTE_CONFIG_V2_PREVIEW) && !defined(IS_SIMULATOR)
#include "config_v2_single_active_commit.hpp"
#include "config_v2_manifest_format.hpp"
#include "config_v2_littlefs_objects.hpp"
#include "config_v2_graph_validator.hpp"
#include "config_v2_digest.hpp"
#include "config_v2_stage_policy.hpp"
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <algorithm>
#include <string>

namespace config_v2_single_active_commit {
namespace {
constexpr const char *kRoot = "/omote-v2";
constexpr const char *kManifests = "/omote-v2/manifests";

std::string manifestPath(const std::string &sha) {
  return std::string(kManifests) + "/" + sha + ".json";
}
bool readManifest(const std::string &sha, std::string &bytes) {
  bytes.clear();
  if (!config_v2_digest::valid(sha))
    return false;
  File f = LittleFS.open(manifestPath(sha).c_str(), "r");
  if (!f || f.isDirectory() || !f.size() ||
      f.size() > config_v2_manifest_format::kMaxManifestBytes)
    return false;
  bytes.reserve(f.size());
  while (f.available()) {
    uint8_t buffer[512];
    const auto n = f.read(buffer, std::min<size_t>(sizeof(buffer), f.available()));
    if (!n) { f.close(); return false; }
    bytes.append(reinterpret_cast<const char *>(buffer), n);
    yield();
  }
  f.close();
  config_v2_manifest_format::Manifest manifest;
  return config_v2_manifest_format::decode(bytes, manifest) &&
         manifest.revision == sha;
}
bool verifyAll(const std::vector<config_v2_manifest_index::Entry> &files) {
  for (const auto &file : files)
    if (config_v2_littlefs_objects::verifyObject(file.sha256, file.length) !=
        config_v2_littlefs_objects::Status::Ok)
      return false;
  return true;
}
} // namespace

Status prepare(const std::vector<config_v2_manifest_index::Entry> &files,
               Prepared &out) {
  out = Prepared{};
  std::string canonical, revision;
  if (!config_v2_manifest_format::encode(files, canonical, revision))
    return Status::Invalid;
  if (!verifyAll(files))
    return Status::MissingObject;

  // The graph validator also checks exact bytes, JSONC, dependencies,
  // widget/image references. Run on a *non-UI* task when integrated.
  auto load = [&](const std::string &path, std::string &content) {
    const auto it = std::find_if(files.begin(), files.end(), [&](const auto &e) {
      return e.path == path;
    });
    return it != files.end() &&
           config_v2_littlefs_objects::readObject(
               it->sha256, content, it->length);
  };
  if (!config_v2_graph_validator::validate(files, load).ok())
    return Status::GraphInvalid;
  const auto path = manifestPath(revision);
  if (LittleFS.exists(path.c_str())) {
    std::string old;
    if (!readManifest(revision, old) || old != canonical)
      return Status::CorruptManifest;
    out = {revision, static_cast<unsigned>(files.size())};
    return Status::Ok;
  }

  if (!config_v2_stage_policy::hasCapacity(
          LittleFS.totalBytes(), LittleFS.usedBytes(), canonical.size()))
    return Status::NoSpace;
  if ((!LittleFS.exists(kRoot) && !LittleFS.mkdir(kRoot)) ||
      (!LittleFS.exists(kManifests) && !LittleFS.mkdir(kManifests)))
    return Status::IoError;
  // Temporary manifest is NOT active. A power loss here leaves the current
  // NVS key unchanged. Never overwrite an existing published manifest.
  const auto tmp = std::string(kManifests) + "/" + revision + ".tmp";
  if (LittleFS.exists(tmp.c_str()))
    return Status::Conflict; // Clean orphan temp only via future safe GC.
  File f = LittleFS.open(tmp.c_str(), "w");
  if (!f)
    return Status::IoError;
  const bool written = f.write(
      reinterpret_cast<const uint8_t *>(canonical.data()), canonical.size()) ==
      canonical.size();
  f.flush();
  f.close();
  if (!written) {
    LittleFS.remove(tmp.c_str());
    return Status::IoError;
  }
  if (!LittleFS.rename(tmp.c_str(), path.c_str()))
    return Status::IoError;
  std::string verified;
  if (!readManifest(revision, verified) || verified != canonical)
    return Status::CorruptManifest;
  out = {revision, static_cast<unsigned>(files.size())};
  return Status::Ok;
}

Status activatePrepared(const Prepared &prepared,
                        const std::string &expectedActive,
                        config_v2_local_approval::Gate &physicalGate,
                        const config_v2_local_approval::Request &request,
                        std::uint32_t nowMs) {
  // The future authenticated protocol must independently establish peer
  // identity and HA-admin consent; this gate protects the storage boundary
  // against commits lacking a matching, one-time local touchscreen approval.
  if (!config_v2_local_approval::requestIsValid(request) ||
      request.proposedSha256 != prepared.revision ||
      request.expectedActiveSha256 != expectedActive)
    return Status::PhysicalApprovalMissing;
  if (!config_v2_digest::valid(prepared.revision) ||
      (!expectedActive.empty() && !config_v2_digest::valid(expectedActive)))
    return Status::Invalid;
  std::string bytes;
  if (!readManifest(prepared.revision, bytes))
    return Status::CorruptManifest;
  config_v2_manifest_format::Manifest manifest;
  if (!config_v2_manifest_format::decode(bytes, manifest) ||
      manifest.files.size() != prepared.files || !verifyAll(manifest.files))
    return Status::MissingObject;

  Preferences nvs;
  if (!nvs.begin("omotecfg", false))
    return Status::IoError;
  // Compare the *persisted* current pointer, never a cached HTTP status.
  const bool hasActive = nvs.isKey("active");
  const std::string current = hasActive ?
      std::string(nvs.getString("active", "").c_str()) : std::string();
  if (current != expectedActive) {
    nvs.end();
    return Status::Conflict;
  }
  if (!physicalGate.consumeForAuthenticatedCommit(request, nowMs)) {
    nvs.end();
    return Status::PhysicalApprovalMissing;
  }
  // One pointer only. ESP NVS power-loss semantics and actual restore must
  // be hardware-qualified before secure service may invoke this method.
  const bool wrote = nvs.putString("active", prepared.revision.c_str()) ==
      prepared.revision.size();
  const bool matches = wrote &&
      std::string(nvs.getString("active", "").c_str()) == prepared.revision;
  nvs.end();
  return matches ? Status::Ok : Status::IoError;
}
} // namespace config_v2_single_active_commit
#endif
