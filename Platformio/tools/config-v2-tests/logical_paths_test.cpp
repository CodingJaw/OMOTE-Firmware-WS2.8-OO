#include "../../lib/esp32HalImpl/wifiHandler/config_v2_logical_paths.hpp"
#include <cassert>
#include <iostream>
#include <string>
using config_v2_logical_paths::inDirectory;

int main() {
  assert(inDirectory("Devices/Roku.json", "Devices") == "Devices/Roku.json");
  assert(inDirectory("/littlefs/Devices/Roku.json", "Devices") == "Devices/Roku.json");
  assert(inDirectory("Actions/IRActionExample.json", "Actions") == "Actions/IRActionExample.json");
  assert(inDirectory("/littlefs/Actions/IRActionExample.json", "Actions") == "Actions/IRActionExample.json");
  assert(inDirectory("/littlefs/Devices/Roku.json", "Actions").empty());
  assert(inDirectory("Devices/Roku.json", "Actions").empty());
  assert(inDirectory("Devices", "Devices").empty());
  assert(inDirectory("Devices/../HaSettings.json", "Devices").empty());
  assert(inDirectory("/littlefs/omote-v2/objects/" + std::string(64, 'a'),
                     "Devices").empty());
  assert(inDirectory("/tmp/Devices/Roku.json", "Devices").empty());
  assert(inDirectory("Devices//Roku.json", "Devices").empty());
  assert(inDirectory("Devices/Roku.json/", "Devices").empty());
  assert(inDirectory("Devices/Roku.JSON", "Devices").empty());
  assert(inDirectory("Devices/../Roku.json", "Devices").empty());
  assert(inDirectory("Devices/Roku.json%2fHaSettings.json", "Devices").empty());
  assert(inDirectory("Actions/IRActionExample.json", "Devices").empty());
  assert(inDirectory("Devicesevil/Roku.json", "Devices").empty());
  assert(inDirectory("HaSettings.json", "Devices").empty());
  std::cout << "config v2 active logical paths: PASS\n";
}
