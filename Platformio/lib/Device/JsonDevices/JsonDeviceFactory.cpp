#include "JsonDevices/JsonDeviceFactory.hpp"
#include "HardwareFactory.hpp"
#include "JsonDevices/JsonDevice.hpp"
#if defined(OMOTE_CONFIG_V2_PREVIEW) && !defined(IS_SIMULATOR)
#include "config_v2_logical_paths.hpp"
#include "config_v2_single_active_runtime.hpp"
#endif

namespace Json {

IDevice::Ptr JsonDeviceFactory::Create(const std::filesystem::path &aDeviceFile) {
  auto device = std::make_shared<JsonDevice>(aDeviceFile);
  return device->isValid() ? device : nullptr;
}

std::vector<std::shared_ptr<IDevice>> JsonDeviceFactory::getDevices(const std::filesystem::path &aDevicesDirectory) {
#if defined(OMOTE_CONFIG_V2_PREVIEW) && !defined(IS_SIMULATOR)
  const auto mode = config_v2_single_active_runtime::snapshot().mode;
  if (mode == config_v2_single_active_runtime::Mode::Recovery)
    return {};
  if (mode == config_v2_single_active_runtime::Mode::Active) {
    // The active snapshot is an immutable object index, not a directory.
    // Never enumerate the legacy root when a v2 revision is active.
    if (aDevicesDirectory != std::filesystem::path(FS_PATH "Devices"))
      return {};
    std::vector<std::shared_ptr<IDevice>> devices;
    for (const auto &logical : config_v2_single_active_runtime::publicPaths()) {
      if (config_v2_logical_paths::inDirectory(logical, "Devices").empty())
        continue;
      if (auto device = Create(std::filesystem::path(logical)); device)
        devices.push_back(std::move(device));
    }
    return devices;
  }
#endif
  if (!std::filesystem::is_directory(aDevicesDirectory))
    return {};

  std::vector<std::shared_ptr<IDevice>> devices;
  for (auto &entry : std::filesystem::directory_iterator(aDevicesDirectory)) {
    if (auto device = Create(entry.path()); device) {
      devices.push_back(device);
    }
  }
  return devices;
}

} // namespace Json
