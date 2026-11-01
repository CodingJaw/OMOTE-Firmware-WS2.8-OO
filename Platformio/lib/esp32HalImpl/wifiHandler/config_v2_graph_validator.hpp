#pragma once

// Validation of a candidate OMOTE JSON scene/page/command graph.
// No Arduino or network dependencies. Uses the same RapidJSON comment
// parsing mode as RapidJsonUtility. The loader is a user-supplied callback
// that returns exact immutable object bytes by logical config path.
// Do not run this synchronous validator on the LVGL/touch thread.
#include "config_v2_manifest_index.hpp"
#include "config_v2_sha256.hpp"
#include <rapidjson/document.h>
#include <map>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace config_v2_graph_validator {

enum class Error {
  None,
  InvalidIndex,
  MissingFile,
  LengthMismatch,
  HashMismatch,
  InvalidJson,
  MissingScene,
  MissingPage,
  MissingCommandFile,
  MissingCommand,
  InvalidStructure,
  InvalidWidget,
  MissingImage
};

struct Result {
  Error error = Error::None;
  std::string path;  // Logical path only. Never log file contents/tokens.
  bool ok() const { return error == Error::None; }
};

inline bool memberString(const rapidjson::Value &obj, const char *key,
                         std::string &out) {
  if (!obj.IsObject() || !obj.HasMember(key) || !obj[key].IsString())
    return false;
  out = obj[key].GetString();
  return !out.empty();
}

inline bool hasFile(const std::map<std::string, config_v2_manifest_index::Entry> &idx,
                    const std::string &path) {
  return idx.find(path) != idx.end();
}

inline std::string resolvePage(
    const std::map<std::string, config_v2_manifest_index::Entry> &idx,
    std::string_view path) {
  const std::string direct(path);
  if (hasFile(idx, direct))
    return direct;
  if (path.substr(0, 6) != "Pages/") {
    const std::string fallback = "Pages/" + direct;
    if (hasFile(idx, fallback))
      return fallback;
  }
  return {};
}

inline bool recognizedWidget(std::string_view name) {
  constexpr std::string_view types[] = {
      "Title", "Label", "Button", "Image", "ColorButtons", "NumberPad",
      "HaToggle", "HaLabel", "HaSwitch", "HaSlider", "HaMomentary", "HaClimate"};
  for (auto item : types)
    if (item == name)
      return true;
  return false;
}

// If a widget explicitly refers to an IR/MQTT command, validate that name.
// Empty command fields are allowed only where the widget has no command.
inline bool commandExists(const std::unordered_set<std::string> &names,
                          const std::string &prefix, const rapidjson::Value &value) {
  if (value.IsString())
    return names.find(prefix + value.GetString()) != names.end();
  if (value.IsArray()) {
    for (const auto &item : value.GetArray())
      if (!item.IsString() ||
          names.find(prefix + item.GetString()) == names.end())
        return false;
    return true;
  }
  return false;
}

template <typename Load>
Result validate(const std::vector<config_v2_manifest_index::Entry> &entries,
                Load load) {
  const auto indexStatus = config_v2_manifest_index::validate(entries);
  if (!indexStatus.ok())
    return {Error::InvalidIndex, {}};

  std::map<std::string, config_v2_manifest_index::Entry> index;
  for (const auto &e : entries)
    index.emplace(e.path, e);

  // Every entry is checked against the exact bytes listed in the manifest.
  // This implementation loads one file at a time; production call sites
  // must budget RAM and use bounded/chunked file hashing instead.
  for (const auto &e : entries) {
    std::string bytes;
    if (!load(e.path, bytes))
      return {Error::MissingFile, e.path};
    if (bytes.size() != e.length)
      return {Error::LengthMismatch, e.path};
    config_v2_sha256::Hasher hash;
    if (!hash.update(bytes) || hash.hex() != e.sha256)
      return {Error::HashMismatch, e.path};
    if (config_v2_path_policy::jsonFile(e.path)) {
      rapidjson::Document d;
      d.Parse<rapidjson::kParseCommentsFlag>(bytes.data(), bytes.size());
      if (d.HasParseError() || !d.IsObject())
        return {Error::InvalidJson, e.path};
    }
  }

  // Loader can be backed by an immutable content-addressed object index.
  // Re-check the loader result is syntactically an object at dereference.
  auto parse = [&](const std::string &path, rapidjson::Document &doc) -> bool {
    std::string content;
    if (!load(path, content))
      return false;
    doc.Parse<rapidjson::kParseCommentsFlag>(content.data(), content.size());
    return !doc.HasParseError() && doc.IsObject();
  };
  rapidjson::Document registry;
  if (!parse("Scenes.json", registry) || !registry.HasMember("Scenes") ||
      !registry["Scenes"].IsArray() || registry["Scenes"].Empty())
    return {Error::InvalidStructure, "Scenes.json"};

  std::map<std::string, std::unordered_set<std::string>> commandCache;
  auto getCommands = [&](const std::string &path) -> bool {
    if (commandCache.find(path) != commandCache.end())
      return true;
    if (!hasFile(index, path))
      return false;
    rapidjson::Document d;
    if (!parse(path, d) || !d.HasMember("Commands") || !d["Commands"].IsArray())
      return false;
    std::unordered_set<std::string> names;
    for (const auto &entry : d["Commands"].GetArray()) {
      std::string name, mode, protocol;
      if (!memberString(entry, "Command", name) ||
          !memberString(entry, "Mode", mode) ||
          !memberString(entry, "Protocol", protocol) ||
          !entry.HasMember("Data") || !entry["Data"].IsArray())
        return false;
      if (!names.insert(name).second)
        return false;
    }
    commandCache.emplace(path, std::move(names));
    return true;
  };

  for (const auto &item : registry["Scenes"].GetArray()) {
    std::string scenePath;
    if (!memberString(item, "FileName", scenePath) ||
        !hasFile(index, scenePath))
      return {Error::MissingScene, scenePath};
    rapidjson::Document scene;
    if (!parse(scenePath, scene) || !scene.HasMember("Pages") ||
        !scene["Pages"].IsArray() || scene["Pages"].Empty())
      return {Error::InvalidStructure, scenePath};

    for (const char *sequenceName : {"StartCommandSequence", "ExitCommandSequence"}) {
      if (!scene.HasMember(sequenceName))
        continue;
      if (!scene[sequenceName].IsArray())
        return {Error::InvalidStructure, scenePath};
      for (const auto &command : scene[sequenceName].GetArray()) {
        std::string commandFile, commandName;
        if (!memberString(command, "CommandFile", commandFile) ||
            !memberString(command, "Command", commandName) ||
            !getCommands(commandFile))
          return {Error::MissingCommandFile, scenePath};
        if (commandCache.at(commandFile).count(commandName) == 0)
          return {Error::MissingCommand, scenePath};
      }
    }

    for (const auto &pageEntry : scene["Pages"].GetArray()) {
      std::string requestedPage;
      if (!memberString(pageEntry, "FileName", requestedPage))
        return {Error::MissingPage, scenePath};
      const auto pagePath = resolvePage(index, requestedPage);
      if (pagePath.empty())
        return {Error::MissingPage, requestedPage};
      std::string prefix;
      if (pageEntry.IsObject() && pageEntry.HasMember("CommandPrefix")) {
        if (!pageEntry["CommandPrefix"].IsString())
          return {Error::InvalidStructure, scenePath};
        prefix = pageEntry["CommandPrefix"].GetString();
      }
      rapidjson::Document page;
      if (!parse(pagePath, page))
        return {Error::InvalidJson, pagePath};
      if (!page.HasMember("Widgets") || !page["Widgets"].IsArray())
        return {Error::InvalidStructure, pagePath};
      std::string commandsPath;
      const bool hasCommandFile = memberString(page, "CommandFile", commandsPath);
      if (hasCommandFile && !getCommands(commandsPath))
        return {Error::MissingCommandFile, pagePath};

      for (const auto &widget : page["Widgets"].GetArray()) {
        std::string type;
        if (!memberString(widget, "Type", type) || !recognizedWidget(type))
          return {Error::InvalidWidget, pagePath};
        if (type == "Image") {
          std::string image;
          if (!memberString(widget, "FileName", image) ||
              !config_v2_path_policy::imageAsset(image) ||
              !hasFile(index, image))
            return {Error::MissingImage, pagePath};
        }
        if (widget.HasMember("Command")) {
          if (!hasCommandFile)
            return {Error::MissingCommandFile, pagePath};
          if (!commandExists(commandCache.at(commandsPath), prefix, widget["Command"]))
            return {Error::MissingCommand, pagePath};
        }
      }
      if (page.HasMember("ButtonMaps")) {
        if (!page["ButtonMaps"].IsObject())
          return {Error::InvalidStructure, pagePath};
        for (auto key = page["ButtonMaps"].MemberBegin();
             key != page["ButtonMaps"].MemberEnd(); ++key) {
          if (!key->value.IsObject())
            return {Error::InvalidStructure, pagePath};
          for (auto action = key->value.MemberBegin();
               action != key->value.MemberEnd(); ++action) {
            if (!hasCommandFile)
              return {Error::MissingCommandFile, pagePath};
            if (!commandExists(commandCache.at(commandsPath), prefix, action->value))
              return {Error::MissingCommand, pagePath};
          }
        }
      }
    }
  }
  return {};
}

} // namespace config_v2_graph_validator
