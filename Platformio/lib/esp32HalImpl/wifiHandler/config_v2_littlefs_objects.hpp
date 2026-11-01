#pragma once

// Inert until called by a future AUTHENTICATED v2 transaction service.
// All writes are restricted to /omote-v2/staging/<id>/<hash>.part; existing
// active and root LittleFS JSON paths are never truncated.
#include <cstddef>
#include <cstdint>
#include <string>

namespace config_v2_littlefs_objects {
enum class Status {
  Ok, Invalid, Missing, NotEnoughSpace, IoError, OffsetConflict,
  InvalidDigest, ExistingObjectInvalid
};

Status begin(const std::string &transactionId, const std::string &hash,
             uint64_t length);
Status append(const std::string &transactionId, const std::string &hash,
              uint64_t offset, const uint8_t *data, std::size_t count,
              uint64_t totalLength);
Status complete(const std::string &transactionId, const std::string &hash,
                uint64_t length);
Status verifyObject(const std::string &hash, uint64_t length);
bool readObject(const std::string &hash, std::string &out,
                uint64_t maximumBytes);
} // namespace config_v2_littlefs_objects
