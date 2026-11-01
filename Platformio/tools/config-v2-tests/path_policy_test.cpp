// Run: c++ -std=c++17 -Wall -Wextra -Werror -o /tmp/omote-path-test
//          Platformio/tools/config-v2-tests/path_policy_test.cpp && /tmp/omote-path-test
#include "../../lib/esp32HalImpl/wifiHandler/config_v2_path_policy.hpp"
#include <cassert>
#include <iostream>

using namespace config_v2_path_policy;

int main() {
  assert(publicReadable("Scenes.json"));
  assert(publicReadable("Scenes/Scene_WatchTV.json"));
  assert(publicReadable("Pages/Page_TV.json"));
  assert(publicReadable("Commands/Commands_Roku.json"));
  assert(publicReadable("Devices/Roku.json"));
  assert(publicReadable("Actions/IRActionExample.json"));
  assert(publicReadable("Images/OMOTE_Logo.png"));
  assert(publicReadable("Images/splash.jpg"));
  assert(!publicReadable("Images/exploit.js"));
  assert(!publicReadable("Images/../../HaSettings.json"));
  assert(publicReadable("DeviceSettings.schema.json"));
  assert(!publicReadable("DeviceSettings.json"));
  assert(!publicReadable("HaSettings.json"));
  assert(!publicReadable("HaStateCache.json"));
  assert(!publicReadable("mqtt.json"));
  assert(!publicReadable("ftp.json"));
  assert(!publicReadable("ntp.json"));
  assert(!publicReadable("config-v2/active.json"));
  assert(!publicReadable("Scenes/binary.bin"));
  assert(!publicReadable("/Scenes.json"));
  assert(!publicReadable("Scenes/../HaSettings.json"));
  assert(!publicReadable("Scenes//Scene_TV.json"));
  assert(!publicReadable("Scenes/./Scene_TV.json"));
  assert(!publicReadable("Scenes/Scene_TV.json/"));
  assert(!publicReadable("Scenes\\Scene_TV.json"));
  assert(!publicReadable("Scenes/%2e%2e/HaSettings.json"));
  assert(!publicReadable("Scenes/Scene_TV.JSON"));
  assert(!publicReadable("Scenes/Scene_TV.json:stream"));

  assert(deployable("Scenes.json"));
  // Private per-device configuration is never part of a portable manifest.
  // It will require a separate encrypted, explicit opt-in protocol.
  assert(!deployable("HaSettings.json"));
  assert(!deployable("DeviceSettings.json"));
  assert(!deployable("mqtt.json"));
  assert(!deployable("ftp.json"));
  assert(!deployable("ntp.json"));
  assert(!deployable("HaStateCache.json"));
  assert(!deployable("config-v2/active.json"));
  assert(!deployable("Scenes/../HaSettings.json"));
  assert(containsSecrets("HaSettings.json"));
  assert(containsSecrets("DeviceSettings.json"));
  assert(!containsSecrets("Scenes.json"));
  std::cout << "config v2 path policy: PASS" << std::endl;
}
