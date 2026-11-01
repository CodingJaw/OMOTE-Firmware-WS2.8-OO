#pragma once
// Read-only boot-time resolver for ONE active configuration; no A/B, no rollback.
// This module cannot create/select a revision or authorize any network mutation.
// Only compiled into the Waveshare Config v2 preview build.
#include <string>

namespace config_v2_single_active_runtime {

enum class Mode { Legacy, Active, Recovery };
struct Snapshot {
  Mode mode = Mode::Legacy;
  std::string revision;
  std::string reason;  // Non-secret diagnostic code, not user JSON.
  unsigned fileCount = 0;
};

// Call once after LittleFS.begin(false), before constructing JsonUI.
// No remote API may call this to switch revisions during a live scene.
void beginReadOnly(bool filesystemMounted);
Snapshot snapshot();
// Empty path on invalid or unlisted active file; no implicit legacy fallback.
std::string resolveVfsPath(const std::string &logicalPath);
// True only when a usable Scenes.json is available through the selected view.
bool sceneReady();

} // namespace config_v2_single_active_runtime
