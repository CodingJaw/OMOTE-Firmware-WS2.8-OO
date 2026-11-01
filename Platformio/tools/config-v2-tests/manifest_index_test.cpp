// Host-only structural validation. Does not imply JSON syntax or hardware
// reference validation has been implemented.
#include "../../lib/esp32HalImpl/wifiHandler/config_v2_manifest_index.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace config_v2_manifest_index;

int main() {
  const std::string a(64, 'a'), b(64, 'b');
  std::vector<Entry> files = {
      {"Scenes.json", a, 512},
      {"Scenes/Scene_WatchTV.json", b, 480},
      {"Pages/Page_TV.json", b, 920},
      {"Commands/Commands_Roku.json", a, 240},
      {"Devices/Roku.json", a, 120},
      {"Actions/IRActionExample.json", b, 420}};
  Result r = validate(files);
  assert(r.ok());
  assert(r.totalBytes == 2692);
  assert(r.secretFiles == 0);

  files.push_back({"Devices/Roku.json", a, 120});
  assert(validate(files).error == Error::DuplicatePath);
  files.pop_back();
  for (const char *privatePath : {"HaSettings.json", "DeviceSettings.json",
                                  "mqtt.json", "ntp.json", "ftp.json",
                                  "HaStateCache.json"}) {
    files[4].path = privatePath;
    assert(validate(files).error == Error::ForbiddenPath);
  }
  files[4].path = "Devices/Roku.json";
  files[0].sha256 = "not-a-hash";
  assert(validate(files).error == Error::InvalidHash);
  files[0].sha256 = a;
  files[0].length = 0;
  assert(validate(files).error == Error::InvalidSize);
  files[0].length = kMaxFileBytes + 1;
  assert(validate(files).error == Error::InvalidSize);
  files[0].length = 512;
  files[0].path = "Other.json";
  assert(validate(files).error == Error::ForbiddenPath);
  files[0].path = "Scenes.json";

  files.erase(files.begin());
  assert(validate(files).error == Error::MissingSceneRegistry);
  files.clear();
  assert(validate(files).error == Error::NoFiles);
  for (std::size_t i = 0; i <= kMaxEntries; ++i)
    files.push_back({"Scenes.json", a, 100});
  assert(validate(files).error == Error::TooManyFiles);

  files.clear();
  files.push_back({"Scenes.json", a, 20});
  files.push_back({"Pages/../HaSettings.json", a, 20});
  assert(validate(files).error == Error::ForbiddenPath);

  std::cout << "config v2 manifest index: PASS" << std::endl;
}
