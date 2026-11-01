#pragma once

// Pure, host-testable generation selection logic. This is deliberately not
// wired into boot or NVS until the storage resolver and power-cut tests exist.
// The caller MUST persist the changed selection before attempting UI startup.
#include <cstdint>
#include <string>
#include <string_view>

namespace config_v2_generation {

constexpr uint32_t kMaxTrialBoots = 2;

inline bool revisionIdValid(std::string_view digest) {
  if (digest.size() != 64)
    return false;
  for (char c : digest)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return false;
  return true;
}

struct Selection {
  std::string active;    // Last confirmed good generation
  std::string previous;  // Last confirmed rollback generation
  std::string pending;   // Validated generation under trial
  uint32_t trialBoots = 0;
};

enum class BootSource { Pending, Active, Previous, Recovery };

struct BootChoice {
  BootSource source;
  std::string revision;
};

// Only stage a new trial while both the active and previous generation remain
// immutable. Calling beginTrial never changes the confirmed active selection.
inline bool beginTrial(Selection &state, std::string_view revision) {
  if (!revisionIdValid(revision) || !state.pending.empty() ||
      revision == state.active || revision == state.previous)
    return false;
  state.pending.assign(revision.data(), revision.size());
  state.trialBoots = 0;
  return true;
}

// Call exactly once per boot. The caller checks manifests and object hashes;
// a revision is usable only when all referenced files verify.
template <typename RevisionUsable>
BootChoice chooseBoot(Selection &state, RevisionUsable usable) {
  if (!state.pending.empty()) {
    if (state.trialBoots < kMaxTrialBoots && usable(state.pending)) {
      ++state.trialBoots; // MUST persist before initializing UI.
      return {BootSource::Pending, state.pending};
    }
    state.pending.clear();
    state.trialBoots = 0;
  }
  if (revisionIdValid(state.active) && usable(state.active))
    return {BootSource::Active, state.active};
  if (revisionIdValid(state.previous) && usable(state.previous)) {
    state.active = state.previous;
    state.previous.clear();
    return {BootSource::Previous, state.active};
  }
  return {BootSource::Recovery, {}};
}

// Only the firmware's local UI health monitor may confirm a pending revision.
// Home Assistant connectivity/acknowledgment must not influence this.
inline bool confirmUiHealthy(Selection &state, std::string_view bootedRevision) {
  if (state.pending.empty() || state.pending != bootedRevision)
    return false;
  state.previous = state.active;
  state.active = state.pending;
  state.pending.clear();
  state.trialBoots = 0;
  return true;
}

inline void markLocalUiFailure(Selection &state) {
  state.pending.clear();
  state.trialBoots = 0;
}

// Manual rollback is authenticated and confirmed at a higher layer.
inline bool rollback(Selection &state) {
  if (!revisionIdValid(state.previous) || !state.pending.empty())
    return false;
  state.active.swap(state.previous);
  state.trialBoots = 0;
  return true;
}

} // namespace config_v2_generation
