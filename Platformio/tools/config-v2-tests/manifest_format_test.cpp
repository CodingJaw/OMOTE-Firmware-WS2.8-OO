#include "../../lib/esp32HalImpl/wifiHandler/config_v2_manifest_format.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
using namespace config_v2_manifest_format;
using config_v2_manifest_index::Entry;

int main() {
  std::vector<Entry> files = {
      {"Commands/TV.json", std::string(64, 'a'), 90},
      {"Images/logo.png", std::string(64, 'b'), 48},
      {"Scenes.json", std::string(64, 'c'), 55}};
  std::string json, revision;
  assert(encode(files, json, revision));
  assert(revision.size() == 64);
  Manifest m;
  assert(decode(json, m));
  assert(m.files.size() == 3);
  assert(m.revision == revision);
  assert(m.files[0].path == "Commands/TV.json");
  assert(m.files[1].length == 48);
  // An identical graph with noncanonical whitespace or duplicate/extra keys
  // must be rejected rather than acquiring a distinct revision digest.
  assert(!decode(json + "\\n", m));
  assert(!decode(std::string(" ") + json, m));
  std::string extra = json;
  const auto head = extra.find("\\"files\\":");
  assert(head != std::string::npos);
  extra.insert(head, "\\"extra\\":true,");
  assert(!decode(extra, m));
  std::string tampered = json;
  tampered[0] = 'X';
  assert(!decode(tampered, m));
  tampered = json;
  auto where = tampered.find("Commands/TV.json");
  assert(where != std::string::npos);
  tampered.replace(where, std::string("Commands/TV.json").size(), "Commands/xx.json");
  Manifest changed;
  assert(decode(tampered, changed));
  assert(changed.revision != revision);
  std::swap(files[0], files[1]);
  assert(!encode(files, json, revision));
  files[0].sha256 = "garbage";
  assert(!encode(files, json, revision));
  files.clear();
  assert(!encode(files, json, revision));
  std::cout << "config v2 immutable manifest: PASS" << std::endl;
}
