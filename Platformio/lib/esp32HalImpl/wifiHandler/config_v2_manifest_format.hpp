#pragma once

// Canonical immutable generation manifests, separate from user scene JSON.
// Manifests must be strict JSON (no comments) and paths sorted ascending.
// SHA-256 of the exact manifest bytes is the immutable generation revision.
// Does not read flash or switch the active generation.
#include "config_v2_manifest_index.hpp"
#include "config_v2_sha256.hpp"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <string>
#include <vector>

namespace config_v2_manifest_format {

constexpr const char *kFormat = "omote-config-generation-v1";
constexpr std::size_t kMaxManifestBytes = 32u * 1024u;

struct Manifest {
  std::vector<config_v2_manifest_index::Entry> files;
  std::string revision;
};

inline bool decode(const std::string &bytes, Manifest &out) {
  if (bytes.empty() || bytes.size() > kMaxManifestBytes)
    return false;
  rapidjson::Document doc;
  doc.Parse(bytes.data(), bytes.size());
  if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("format") ||
      !doc["format"].IsString() || std::string(doc["format"].GetString()) != kFormat ||
      !doc.HasMember("files") || !doc["files"].IsArray())
    return false;
  std::vector<config_v2_manifest_index::Entry> entries;
  for (const auto &item : doc["files"].GetArray()) {
    if (!item.IsObject() || !item.HasMember("path") ||
        !item["path"].IsString() || !item.HasMember("sha256") ||
        !item["sha256"].IsString() || !item.HasMember("length") ||
        !item["length"].IsUint64())
      return false;
    config_v2_manifest_index::Entry entry;
    entry.path = item["path"].GetString();
    entry.sha256 = item["sha256"].GetString();
    entry.length = item["length"].GetUint64();
    if (!entries.empty() && entries.back().path >= entry.path)
      return false;
    entries.push_back(std::move(entry));
  }
  if (!config_v2_manifest_index::validate(entries).ok())
    return false;
  config_v2_sha256::Hasher hash;
  if (!hash.update(bytes))
    return false;
  out.files = std::move(entries);
  out.revision = hash.hex();
  return true;
}

inline bool encode(const std::vector<config_v2_manifest_index::Entry> &files,
                   std::string &bytes, std::string &revision) {
  bytes.clear();
  revision.clear();
  if (!config_v2_manifest_index::validate(files).ok())
    return false;
  for (std::size_t i = 1; i < files.size(); ++i)
    if (files[i - 1].path >= files[i].path)
      return false;
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  writer.StartObject();
  writer.Key("format");
  writer.String(kFormat);
  writer.Key("files");
  writer.StartArray();
  for (const auto &item : files) {
    writer.StartObject();
    writer.Key("path");
    writer.String(item.path.data(), static_cast<rapidjson::SizeType>(item.path.size()));
    writer.Key("sha256");
    writer.String(item.sha256.data(), static_cast<rapidjson::SizeType>(item.sha256.size()));
    writer.Key("length");
    writer.Uint64(item.length);
    writer.EndObject();
  }
  writer.EndArray();
  writer.EndObject();
  if (buffer.GetSize() > kMaxManifestBytes)
    return false;
  bytes.assign(buffer.GetString(), buffer.GetSize());
  config_v2_sha256::Hasher hash;
  if (!hash.update(bytes))
    return false;
  revision = hash.hex();
  return true;
}

} // namespace config_v2_manifest_format
