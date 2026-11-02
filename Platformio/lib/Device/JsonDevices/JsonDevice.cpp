#include "JsonDevices/JsonDevice.hpp"
#include "magic_enum.hpp"
#if defined(OMOTE_CONFIG_V2_PREVIEW) && !defined(IS_SIMULATOR)
#include "config_v2_logical_paths.hpp"
#include "config_v2_single_active_runtime.hpp"
#endif
#include <fstream>
#include <sstream>

namespace Json {

JsonDevice::JsonDevice(std::filesystem::path aDeviceJsonFilePath) {
  constexpr auto maxDeviceFileSize = 5000;
  std::filesystem::path actualPath = aDeviceJsonFilePath;
#if defined(OMOTE_CONFIG_V2_PREVIEW) && !defined(IS_SIMULATOR)
  const auto mode = config_v2_single_active_runtime::snapshot().mode;
  if (mode == config_v2_single_active_runtime::Mode::Recovery) {
    mParseResult = ParseResult::FileError;
    return;
  }
  if (mode == config_v2_single_active_runtime::Mode::Active) {
    const std::string logical = config_v2_logical_paths::inDirectory(
        aDeviceJsonFilePath.generic_string(), "Devices");
    if (logical.empty()) {
      mParseResult = ParseResult::FileError;
      return;
    }
    const std::string pinned = config_v2_single_active_runtime::resolveVfsPath(logical);
    if (pinned.empty()) {
      mParseResult = ParseResult::FileError;
      return;
    }
    actualPath = pinned;
    // Persist logical identity, never a stale immutable object hash.
    aDeviceJsonFilePath = logical;
  }
#endif
  std::error_code err;
  const auto bytes = std::filesystem::file_size(actualPath, err);
  if (err || bytes == 0 || bytes > maxDeviceFileSize) {
    mParseResult = ParseResult::FileError;
    return;
  }

  rapidjson::Document deviceJson = OMOTE::JSON::GetDocument(actualPath);
  mParseResult = parse(deviceJson);

  if (mParseResult == ParseResult::Success)
    mFilePath = aDeviceJsonFilePath;
}

JsonDevice::ParseResult JsonDevice::parse(const rapidjson::Document &aDeviceJson) {
  if (!parseGeneralInfo(aDeviceJson)) {
    return ParseResult::GeneralInfoError;
  }
  if (aDeviceJson.HasMember("keys") && !parseKeyActions(aDeviceJson["keys"])) {
    return ParseResult::KeyActionsError;
  }
  return ParseResult::Success;
}

bool JsonDevice::parseGeneralInfo(const rapidjson::Document &aDeviceJson) {
  if (!aDeviceJson.IsObject() || !aDeviceJson.HasMember("name") ||
      !aDeviceJson["name"].IsString() ||
      !aDeviceJson["name"].GetStringLength())
    return false;
  mName = aDeviceJson["name"].GetString();
  mId = DeviceId::None;
  return true;
}

bool JsonDevice::parseKeyActions(const rapidjson::Value &aKeysJson) {
  if (!aKeysJson.IsObject()) {
    return false;
  }
  for (auto keyEnum : magic_enum::enum_values<KeyPressAbstract::KeyId>()) {
    auto keyIdStr = magic_enum::enum_name(keyEnum);
    if (aKeysJson.HasMember(keyIdStr.data())) {
      auto keyAction = std::make_unique<KeyAction>(aKeysJson[keyIdStr.data()]);
      if (keyAction->isValid()) {
        mKeyActions[keyEnum] = std::move(keyAction);
      }
    }
  }
  return true;
}

std::string JsonDevice::GetName() const {
  return mName;
}

lv_color_t JsonDevice::GetDisplayColor() const {
  // TODO Add parsing to specify device color
  return UI::Color::BLUE;
}

bool JsonDevice::IsDeleteAble() const {
  return true;
}

DeviceType JsonDevice::GetType() const {
  return DeviceType::JSON;
}

DeviceId JsonDevice::GetId() const {
  return DeviceId::None;
}

rapidjson::Document JsonDevice::GetExtraConfig() const {
  rapidjson::Document extraInfo;
  auto &alloc = extraInfo.GetAllocator();
  extraInfo.SetObject();

  rapidjson::Value filePath;
  const std::string filePathString = mFilePath.generic_string();
  filePath.SetString(filePathString.c_str(),
                     static_cast<rapidjson::SizeType>(filePathString.size()),
                     alloc);

  extraInfo.AddMember("file_path", filePath, alloc);

  return extraInfo;
}

bool JsonDevice::HandleKeyEvent(KeyPressAbstract::KeyEvent aEvent) {
  if (!mKeyActions.contains(aEvent.mId)) {
    return false;
  }
  // TODO should we have ExecuteAction and execute return bool?
  auto &keyHandler = mKeyActions[aEvent.mId];
  keyHandler->ExecuteAction(aEvent.mType);
  return true;
}

std::unique_ptr<UI::Page::Base> JsonDevice::GetControlPage() {
  // Return nullptr for now - this would need to be implemented based on your UI system
  // Maybe device could define page json?
  return nullptr;
}

bool JsonDevice::isValid() const {
  return mParseResult == ParseResult::Success;
}

} // namespace Json
