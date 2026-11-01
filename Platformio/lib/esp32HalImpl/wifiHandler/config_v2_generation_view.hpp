#pragma once

// A read-only, snapshot-pinned resolver for a single immutable generation.
// This does NOT activate the view on the Waveshare UI. Production must
// replace every root-path reader (scene/page/command/image/settings) together,
// never combining legacy-root and v2 files mid-scene.
#include "config_v2_manifest_format.hpp"
#include "config_v2_path_policy.hpp"
#include "config_v2_generation_selection.hpp"
#include <map>
#include <string>
#include <utility>

namespace config_v2_generation_view {

struct Object {
  std::string sha256;
  uint64_t length = 0;
};

class Pinned {
public:
  bool bind(const config_v2_manifest_format::Manifest &manifest) {
    clear();
    if (!config_v2_generation::revisionIdValid(manifest.revision) ||
        !config_v2_manifest_index::validate(manifest.files).ok())
      return false;
    std::map<std::string, Object> entries;
    for (const auto &file : manifest.files)
      if (!entries.emplace(file.path, Object{file.sha256, file.length}).second)
        return false;
    objects_.swap(entries);
    revision_ = manifest.revision;
    return true;
  }

  const Object *find(const std::string &path) const {
    if (revision_.empty() || !config_v2_path_policy::deployable(path))
      return nullptr;
    auto it = objects_.find(path);
    return it == objects_.end() ? nullptr : &it->second;
  }

  // Literal LittleFS Arduino VFS path, never a path supplied by the client.
  // Must not fall back to the root filesystem for missing generation objects.
  bool vfsPathFor(const std::string &path, std::string &out) const {
    out.clear();
    const Object *object = find(path);
    if (!object)
      return false;
    out = "/littlefs/omote-v2/objects/" + object->sha256;
    return true;
  }

  void clear() {
    revision_.clear();
    objects_.clear();
  }

  bool valid() const { return !revision_.empty(); }
  const std::string &revision() const { return revision_; }
  std::size_t fileCount() const { return objects_.size(); }

private:
  std::string revision_;
  std::map<std::string, Object> objects_;
};

} // namespace config_v2_generation_view
