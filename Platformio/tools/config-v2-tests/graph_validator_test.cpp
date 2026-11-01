#include "../../lib/esp32HalImpl/wifiHandler/config_v2_graph_validator.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using config_v2_graph_validator::Error;
using config_v2_graph_validator::validate;
using config_v2_manifest_index::Entry;

static std::map<std::string, std::string> sample() {
  return {
    {"Scenes.json", R"({"Scenes":[{"SceneName":"TV","FileName":"Scenes/Scene_TV.json"}]})"},
    {"Scenes/Scene_TV.json", R"({
       // Comments are supported by the shipped firmware
       "Type":"Scene",
       "Pages":[{"FileName":"Pages/Page_TV.json","CommandPrefix":"TV_"}],
       "StartCommandSequence":[{"CommandFile":"Commands/TV.json","Command":"POWER"}]
     })"},
    {"Pages/Page_TV.json", R"({
       "CommandFile":"Commands/TV.json",
       "Widgets":[{"Type":"Title"},{"Type":"Button","Command":"UP"},
                  {"Type":"ColorButtons","Command":["RED","BLUE"]},
                  {"Type":"Image","FileName":"Images/logo.png"}],
       "ButtonMaps":{"VolUp":{"Press":"UP","Repeat":"UP"}}
     })"},
    {"Commands/TV.json", R"({"Commands":[
       {"Command":"POWER","Mode":"IR","Protocol":"NEC","Data":["0x10"]},
       {"Command":"TV_UP","Mode":"IR","Protocol":"NEC","Data":["0x11"]},
       {"Command":"TV_RED","Mode":"IR","Protocol":"NEC","Data":["0x12"]},
       {"Command":"TV_BLUE","Mode":"IR","Protocol":"NEC","Data":["0x13"]}
     ]})"},
    {"Images/logo.png", std::string("\x89PNG\r\n", 6)}
  };
}

static std::vector<Entry> manifest(const std::map<std::string, std::string> &files) {
  std::vector<Entry> entries;
  for (const auto &item : files) {
    config_v2_sha256::Hasher hash;
    assert(hash.update(item.second));
    entries.push_back({item.first, hash.hex(), item.second.size()});
  }
  return entries;
}

static config_v2_graph_validator::Result check(
    const std::map<std::string, std::string> &files) {
  const auto entries = manifest(files);
  return validate(entries, [&](const std::string &p, std::string &body) {
    auto it = files.find(p);
    if (it == files.end())
      return false;
    body = it->second;
    return true;
  });
}

int main() {
  auto files = sample();
  assert(check(files).ok());
  // A private settings file cannot be bundled into a portable graph,
  // even when its JSON syntax and digest are valid.
  files["HaSettings.json"] = R"({"Token":"secret-do-not-log"})";
  assert(check(files).error == Error::InvalidIndex);
  files = sample();
  files["Scenes/Scene_TV.json"] = R"({"Pages":[{"FileName":"Pages/Missing.json"}]})";
  assert(check(files).error == Error::MissingPage);
  files = sample();
  files["Scenes.json"] = R"({"Scenes":[{"FileName":"Scenes/Missing.json"}]})";
  assert(check(files).error == Error::MissingScene);
  files = sample();
  files["Pages/Page_TV.json"] = R"({"Widgets":[{"Type":"Unsupported"}]})";
  assert(check(files).error == Error::InvalidWidget);
  files = sample();
  files["Pages/Page_TV.json"] = R"({"CommandFile":"Commands/Missing.json","Widgets":[]})";
  assert(check(files).error == Error::MissingCommandFile);
  files = sample();
  files["Pages/Page_TV.json"] = R"({"CommandFile":"Commands/TV.json","Widgets":[{"Type":"Button","Command":"MISSING"}]})";
  assert(check(files).error == Error::MissingCommand);
  files = sample();
  files.erase("Images/logo.png");
  // Manifest cannot silently omit an image referenced by a page.
  assert(check(files).error == Error::MissingImage);
  files = sample();
  files["Scenes.json"] = R"({"Scenes":[})";
  assert(check(files).error == Error::InvalidJson);
  files = sample();
  auto entries = manifest(files);
  entries[0].sha256 = std::string(64, 'a');
  auto result = validate(entries, [&](const std::string &p, std::string &body) {
    body = files.at(p);
    return true;
  });
  assert(result.error == Error::HashMismatch);
  files = sample();
  entries = manifest(files);
  result = validate(entries, [&](const std::string &p, std::string &body) {
    if (p == "Scenes.json")
      return false;
    body = files.at(p);
    return true;
  });
  assert(result.error == Error::MissingFile);
  std::cout << "config v2 graph validation: PASS" << std::endl;
}
