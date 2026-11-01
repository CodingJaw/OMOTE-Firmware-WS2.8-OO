#include "../../lib/esp32HalImpl/wifiHandler/config_v2_generation_view.hpp"
#include <cassert>
#include <iostream>
#include <string>

using namespace config_v2_generation_view;

int main() {
  config_v2_manifest_format::Manifest a;
  a.revision = std::string(64, 'a');
  a.files = {
    {"Commands/Roku.json", std::string(64,'c'), 32},
    {"Scenes.json", std::string(64,'b'), 101},
    {"Scenes/TV.json", std::string(64,'d'), 80},
    {"HaSettings.json", std::string(64,'e'), 55}
  };
  Pinned pinned;
  assert(pinned.bind(a));
  assert(pinned.valid() && pinned.fileCount()==4);
  const auto publicFiles = pinned.publicPaths();
  assert(publicFiles.size() == 3);
  assert(publicFiles[0] == "Commands/Roku.json");
  assert(publicFiles[1] == "Scenes.json");
  assert(publicFiles[2] == "Scenes/TV.json");
  assert(pinned.revision()==a.revision);
  std::string path;
  assert(pinned.vfsPathFor("Scenes.json", path));
  assert(path == "/littlefs/omote-v2/objects/"+std::string(64,'b'));
  assert(!pinned.vfsPathFor("Pages/Missing.json", path));
  assert(path.empty()); // Must NEVER return the legacy root fallback.
  assert(!pinned.vfsPathFor("../HaSettings.json", path));
  assert(!pinned.vfsPathFor("HaStateCache.json", path));
  assert(pinned.vfsPathFor("HaSettings.json", path));

  auto b = a;
  b.revision = std::string(64,'f');
  b.files[1].sha256 = std::string(64,'0');
  assert(pinned.revision()==a.revision);
  assert(pinned.vfsPathFor("Scenes.json",path));
  assert(path == "/littlefs/omote-v2/objects/"+std::string(64,'b'));
  Pinned second;
  assert(second.bind(b));
  assert(second.vfsPathFor("Scenes.json", path));
  assert(path == "/littlefs/omote-v2/objects/"+std::string(64,'0'));
  assert(pinned.revision()!=second.revision());
  a.revision="garbage";
  assert(!second.bind(a));
  assert(!second.valid());
  assert(second.publicPaths().empty());
  assert(!second.vfsPathFor("Scenes.json",path));
  std::cout << "config v2 pinned generation view: PASS" << std::endl;
}
