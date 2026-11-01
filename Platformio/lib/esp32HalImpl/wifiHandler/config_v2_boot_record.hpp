#pragma once

// Fixed-size, versioned, checksummed generation-selection record.
// No Arduino dependencies. The two redundant slots reside in NVS, not
// LittleFS, so a bad JSON manifest cannot modify or erase the selector.
#include "config_v2_generation_selection.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace config_v2_boot_record {

constexpr std::size_t kRecordBytes = 216;
using Bytes = std::array<uint8_t, kRecordBytes>;
constexpr uint8_t kVersion = 1;
constexpr uint8_t kMagic[4] = {'O', 'M', 'V', '2'};

struct Snapshot {
  config_v2_generation::Selection selection;
  uint64_t sequence = 0; // Monotonically increasing commit counter, not a timestamp.
};

inline uint32_t checksum(const uint8_t *data, std::size_t length) {
  uint32_t crc = 0xffffffffu;
  for (std::size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  }
  return ~crc;
}

inline void put32(Bytes &out, std::size_t offset, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    out[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

inline uint32_t get32(const Bytes &in, std::size_t offset) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i)
    value |= static_cast<uint32_t>(in[offset + i]) << (8 * i);
  return value;
}

inline void put64(Bytes &out, std::size_t offset, uint64_t value) {
  for (unsigned i = 0; i < 8; ++i)
    out[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

inline uint64_t get64(const Bytes &in, std::size_t offset) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i)
    value |= static_cast<uint64_t>(in[offset + i]) << (8 * i);
  return value;
}

inline bool putDigest(Bytes &out, std::size_t offset, const std::string &digest) {
  if (digest.empty())
    return true; // An empty revision is encoded as 64 NUL bytes.
  if (!config_v2_generation::revisionIdValid(digest))
    return false;
  for (std::size_t i = 0; i < 64; ++i)
    out[offset + i] = static_cast<uint8_t>(digest[i]);
  return true;
}

inline bool getDigest(const Bytes &in, std::size_t offset, std::string &out) {
  bool empty = true;
  for (std::size_t i = 0; i < 64; ++i)
    if (in[offset + i] != 0) {
      empty = false;
      break;
    }
  if (empty) {
    out.clear();
    return true;
  }
  out.assign(reinterpret_cast<const char *>(in.data() + offset), 64);
  return config_v2_generation::revisionIdValid(out);
}

inline bool validSelection(const config_v2_generation::Selection &s) {
  using config_v2_generation::revisionIdValid;
  if ((!s.active.empty() && !revisionIdValid(s.active)) ||
      (!s.previous.empty() && !revisionIdValid(s.previous)) ||
      (!s.pending.empty() && !revisionIdValid(s.pending)) ||
      s.trialBoots > config_v2_generation::kMaxTrialBoots)
    return false;
  if ((!s.active.empty() && s.active == s.previous) ||
      (!s.pending.empty() && (s.pending == s.active || s.pending == s.previous)))
    return false;
  if (s.pending.empty() && s.trialBoots != 0)
    return false;
  // A pending revision is only meaningful with an established fallback.
  if (!s.pending.empty() && s.active.empty())
    return false;
  return true;
}

inline bool encode(const Snapshot &snapshot, Bytes &out) {
  if (!validSelection(snapshot.selection) || snapshot.sequence == 0)
    return false;
  out.fill(0);
  for (unsigned i = 0; i < 4; ++i)
    out[i] = kMagic[i];
  out[4] = kVersion;
  // Byte 5 and bytes 6-7 are reserved; all zero in v1.
  put64(out, 8, snapshot.sequence);
  put32(out, 16, snapshot.selection.trialBoots);
  if (!putDigest(out, 20, snapshot.selection.active) ||
      !putDigest(out, 84, snapshot.selection.previous) ||
      !putDigest(out, 148, snapshot.selection.pending))
    return false;
  put32(out, 212, checksum(out.data(), 212));
  return true;
}

inline bool decode(const Bytes &in, Snapshot &snapshot) {
  for (unsigned i = 0; i < 4; ++i)
    if (in[i] != kMagic[i])
      return false;
  if (in[4] != kVersion || in[5] != 0 || in[6] != 0 || in[7] != 0 ||
      checksum(in.data(), 212) != get32(in, 212))
    return false;

  Snapshot candidate;
  candidate.sequence = get64(in, 8);
  candidate.selection.trialBoots = get32(in, 16);
  if (candidate.sequence == 0 ||
      !getDigest(in, 20, candidate.selection.active) ||
      !getDigest(in, 84, candidate.selection.previous) ||
      !getDigest(in, 148, candidate.selection.pending) ||
      !validSelection(candidate.selection))
    return false;
  snapshot = candidate;
  return true;
}

struct Chosen {
  bool valid = false;
  unsigned slot = 0;
  Snapshot snapshot;
};

// Prefer the newest *valid* slot. A partial/corrupt newer slot never hides
// the previous valid one. No automatic wraparound; reject counter exhaustion.
inline Chosen choose(const Bytes &slot0, bool exists0,
                     const Bytes &slot1, bool exists1) {
  Snapshot s0, s1;
  const bool ok0 = exists0 && decode(slot0, s0);
  const bool ok1 = exists1 && decode(slot1, s1);
  if (!ok0 && !ok1)
    return {};
  if (!ok1 || (ok0 && s0.sequence >= s1.sequence))
    return {true, 0, s0};
  return {true, 1, s1};
}

inline bool makeNext(const Chosen &current,
                     const config_v2_generation::Selection &next,
                     Bytes &bytes, unsigned &slot) {
  if (!validSelection(next) ||
      (current.valid &&
       current.snapshot.sequence == std::numeric_limits<uint64_t>::max()))
    return false;
  Snapshot candidate;
  candidate.sequence = current.valid ? current.snapshot.sequence + 1 : 1;
  candidate.selection = next;
  slot = current.valid ? 1u - current.slot : 0u;
  return encode(candidate, bytes);
}

} // namespace config_v2_boot_record
