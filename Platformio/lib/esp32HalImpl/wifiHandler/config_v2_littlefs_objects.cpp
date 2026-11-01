#if defined(OMOTE_CONFIG_V2_PREVIEW) && !defined(IS_SIMULATOR)
#include "config_v2_littlefs_objects.hpp"
#include "config_v2_sha256.hpp"
#include "config_v2_stage_policy.hpp"
#include <Arduino.h>
#include <LittleFS.h>
#include <algorithm>
#include <string>

namespace config_v2_littlefs_objects {
namespace {
constexpr const char *kRoot = "/omote-v2";
constexpr const char *kObjects = "/omote-v2/objects";
constexpr const char *kStaging = "/omote-v2/staging";
constexpr std::size_t kChunk = config_v2_stage_policy::kMaxChunkBytes;

std::string objectPath(const std::string &hash) {
  return std::string(kObjects) + "/" + hash;
}
std::string partPath(const std::string &txid, const std::string &hash) {
  return std::string(kStaging) + "/" + txid + "/" + hash + ".part";
}

bool ensureDirs(const std::string &txid) {
  if (!LittleFS.exists(kRoot) && !LittleFS.mkdir(kRoot))
    return false;
  if (!LittleFS.exists(kObjects) && !LittleFS.mkdir(kObjects))
    return false;
  if (!LittleFS.exists(kStaging) && !LittleFS.mkdir(kStaging))
    return false;
  const auto dir = std::string(kStaging) + "/" + txid;
  return LittleFS.exists(dir.c_str()) || LittleFS.mkdir(dir.c_str());
}

Status verifyPath(const std::string &path, const std::string &hash,
                  uint64_t length) {
  File file = LittleFS.open(path.c_str(), "r");
  if (!file || file.isDirectory())
    return Status::Missing;
  if (file.size() != length) {
    file.close();
    return Status::InvalidDigest;
  }
  config_v2_sha256::Hasher digest;
  uint8_t buffer[kChunk];
  uint64_t readBytes = 0;
  while (readBytes < length) {
    const auto count = static_cast<std::size_t>(
        std::min<uint64_t>(kChunk, length - readBytes));
    const std::size_t got = file.read(buffer, count);
    if (got != count || !digest.update(buffer, got)) {
      file.close();
      return Status::IoError;
    }
    readBytes += got;
    yield(); // Allow Wi-Fi/RTOS task scheduling; LVGL remains on its UI thread.
  }
  file.close();
  return digest.hex() == hash ? Status::Ok : Status::InvalidDigest;
}

bool inputValid(const std::string &tx, const std::string &hash,
                uint64_t size) {
  return config_v2_stage_policy::transactionIdValid(tx) &&
         config_v2_generation::revisionIdValid(hash) &&
         size > 0 && size <= config_v2_stage_policy::kMaxStagedFileBytes;
}
} // namespace

Status begin(const std::string &transactionId, const std::string &hash,
             uint64_t length) {
  if (!inputValid(transactionId, hash, length))
    return Status::Invalid;
  const auto path = partPath(transactionId, hash);
  if (LittleFS.exists(path.c_str())) {
    File existing = LittleFS.open(path.c_str(), "r");
    if (!existing || existing.isDirectory())
      return Status::IoError;
    const bool reasonable = existing.size() <= length;
    existing.close();
    return reasonable ? Status::Ok : Status::Invalid;
  }
  if (!config_v2_stage_policy::hasCapacity(
          LittleFS.totalBytes(), LittleFS.usedBytes(), length))
    return Status::NotEnoughSpace;
  if (!ensureDirs(transactionId))
    return Status::IoError;
  File fresh = LittleFS.open(path.c_str(), "w");
  if (!fresh)
    return Status::IoError;
  fresh.flush();
  fresh.close();
  return Status::Ok;
}

Status append(const std::string &transactionId, const std::string &hash,
              uint64_t offset, const uint8_t *data, std::size_t count,
              uint64_t totalLength) {
  if (!inputValid(transactionId, hash, totalLength) ||
      !data || count == 0)
    return Status::Invalid;
  const auto path = partPath(transactionId, hash);
  File file = LittleFS.open(path.c_str(), "r+");
  if (!file || file.isDirectory())
    return Status::Missing;
  const uint64_t written = file.size();
  const auto decision = config_v2_stage_policy::classifyChunk(
      written, offset, count, totalLength);
  if (decision == config_v2_stage_policy::ChunkDecision::Reject) {
    file.close();
    return Status::OffsetConflict;
  }
  if (!file.seek(static_cast<uint32_t>(offset))) {
    file.close();
    return Status::IoError;
  }
  if (decision == config_v2_stage_policy::ChunkDecision::RetryCompare) {
    uint8_t current[kChunk];
    const bool equal = file.read(current, count) == count &&
                       std::equal(current, current + count, data);
    file.close();
    return equal ? Status::Ok : Status::OffsetConflict;
  }
  const bool ok = file.write(data, count) == count;
  file.flush();
  file.close();
  return ok ? Status::Ok : Status::IoError;
}

Status complete(const std::string &transactionId, const std::string &hash,
                uint64_t length) {
  if (!inputValid(transactionId, hash, length))
    return Status::Invalid;
  const auto part = partPath(transactionId, hash);
  const auto status = verifyPath(part, hash, length);
  if (status != Status::Ok)
    return status;
  const auto target = objectPath(hash);
  if (LittleFS.exists(target.c_str())) {
    const auto verified = verifyPath(target, hash, length);
    if (verified != Status::Ok)
      return Status::ExistingObjectInvalid;
    return LittleFS.remove(part.c_str()) ? Status::Ok : Status::IoError;
  }
  // A single-file rename is enough to publish an immutable content-addressed
  // object; it is NOT sufficient to activate a multi-file generation.
  if (!LittleFS.rename(part.c_str(), target.c_str()))
    return Status::IoError;
  return verifyPath(target, hash, length);
}

Status verifyObject(const std::string &hash, uint64_t length) {
  if (!config_v2_generation::revisionIdValid(hash) ||
      length == 0 || length > config_v2_stage_policy::kMaxStagedFileBytes)
    return Status::Invalid;
  return verifyPath(objectPath(hash), hash, length);
}

bool readObject(const std::string &hash, std::string &out,
                uint64_t maximumBytes) {
  out.clear();
  if (!config_v2_generation::revisionIdValid(hash) ||
      maximumBytes > config_v2_stage_policy::kMaxStagedFileBytes)
    return false;
  File f = LittleFS.open(objectPath(hash).c_str(), "r");
  if (!f || f.isDirectory() || f.size() > maximumBytes)
    return false;
  out.reserve(f.size());
  uint8_t buffer[kChunk];
  while (f.available()) {
    const std::size_t n = f.read(buffer, sizeof(buffer));
    if (!n)
      return false;
    out.append(reinterpret_cast<const char *>(buffer), n);
    yield();
  }
  f.close();
  config_v2_sha256::Hasher digest;
  return digest.update(out) && digest.hex() == hash;
}

} // namespace config_v2_littlefs_objects
#endif
