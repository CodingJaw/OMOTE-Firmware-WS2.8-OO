#if defined(OMOTE_CONFIG_V2_PREVIEW) && !defined(IS_SIMULATOR)
#include "config_v2_single_active_runtime.hpp"
#include "config_v2_generation_view.hpp"
#include "config_v2_littlefs_objects.hpp"
#include "config_v2_manifest_format.hpp"
#include "config_v2_sha256.hpp"
#include "config_v2_path_policy.hpp"
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <rapidjson/document.h>
#include <algorithm>
#include <string>

namespace config_v2_single_active_runtime {
namespace {
Snapshot current{};
config_v2_generation_view::Pinned pinned{};

void recovery(const char *reason) {
  pinned.clear();
  current.mode = Mode::Recovery;
  current.revision.clear();
  current.fileCount = 0;
  current.reason = reason;
}

bool validSceneRegistry(const std::string &path) {
  File f = LittleFS.open(path.c_str(), "r");
  if (!f || f.isDirectory() || f.size() == 0 || f.size() > 256u * 1024u)
    return false;
  // Parse the scene registry before allowing JsonUI to touch user JSON.
  // Bounded: Scenes.json may be at most 256 KiB.
  std::string bytes;
  bytes.reserve(f.size());
  while (f.available()) {
    char buf[512];
    const size_t n = f.readBytes(buf, std::min<size_t>(sizeof(buf), f.available()));
    if (!n) {
      f.close();
      return false;
    }
    bytes.append(buf, n);
    yield();
  }
  f.close();
  rapidjson::Document doc;
  doc.Parse<rapidjson::kParseCommentsFlag>(bytes.data(), bytes.size());
  return !doc.HasParseError() && doc.IsObject() && doc.HasMember("Scenes") &&
         doc["Scenes"].IsArray() && !doc["Scenes"].Empty();
}
} // namespace

void beginReadOnly(bool mounted) {
  pinned.clear();
  current = Snapshot{};
  if (!mounted) {
    recovery("filesystem-unavailable");
    return;
  }

  // The current firmware has no writer for this future namespace.
  // In a factory/legacy device, missing namespace means use existing root.
  Preferences nvs;
  if (!nvs.begin("omotecfg", true)) {
    if (!validSceneRegistry("/Scenes.json"))
      recovery("legacy-scenes-missing");
    return;
  }
  const bool selected = nvs.isKey("active");
  const String value = selected ? nvs.getString("active", "") : String();
  nvs.end();
  if (!selected) {
    if (!validSceneRegistry("/Scenes.json"))
      recovery("legacy-scenes-missing");
    return;
  }

  const std::string requested(value.c_str());
  if (!config_v2_digest::valid(requested)) {
    recovery("invalid-active-pointer");
    return;
  }
  const std::string manifestPath = "/omote-v2/manifests/" + requested + ".json";
  File manifestFile = LittleFS.open(manifestPath.c_str(), "r");
  if (!manifestFile || manifestFile.isDirectory() ||
      manifestFile.size() == 0 ||
      manifestFile.size() > config_v2_manifest_format::kMaxManifestBytes) {
    recovery("active-manifest-missing");
    return;
  }
  std::string bytes;
  bytes.reserve(manifestFile.size());
  while (manifestFile.available()) {
    char buf[512];
    const size_t n = manifestFile.readBytes(buf, std::min<size_t>(sizeof(buf), manifestFile.available()));
    if (!n) {
      manifestFile.close();
      recovery("manifest-read-failed");
      return;
    }
    bytes.append(buf, n);
    yield();
  }
  manifestFile.close();
  config_v2_manifest_format::Manifest manifest;
  if (!config_v2_manifest_format::decode(bytes, manifest) ||
      manifest.revision != requested || !pinned.bind(manifest)) {
    recovery("active-manifest-invalid");
    return;
  }
  // Boot must not expose a manifest that points at missing or damaged objects.
  // Synchronous verification is an initial safety gate, NOT yet performance-qualified.
  for (const auto &entry : manifest.files) {
    if (config_v2_littlefs_objects::verifyObject(entry.sha256, entry.length) !=
        config_v2_littlefs_objects::Status::Ok) {
      recovery("active-object-invalid");
      return;
    }
  }
  const auto object = pinned.find("Scenes.json");
  if (!object || !validSceneRegistry(("/omote-v2/objects/" + object->sha256))) {
    recovery("active-scenes-invalid");
    return;
  }
  current.mode = Mode::Active;
  current.revision = requested;
  current.fileCount = static_cast<unsigned>(manifest.files.size());
  current.reason = "ready";
}

Snapshot snapshot() { return current; }

std::string resolveVfsPath(const std::string &logicalPath) {
  if (!config_v2_path_policy::safeRelative(logicalPath))
    return {};
  if (current.mode == Mode::Recovery)
    return {};
  if (current.mode == Mode::Legacy)
    return "/littlefs/" + logicalPath;
  std::string result;
  return pinned.vfsPathFor(logicalPath, result) ? result : std::string();
}

bool sceneReady() { return current.mode != Mode::Recovery; }

} // namespace config_v2_single_active_runtime
#endif
