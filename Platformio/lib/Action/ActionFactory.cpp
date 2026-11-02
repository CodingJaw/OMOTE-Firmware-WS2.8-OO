#include "ActionFactory.hpp"
#include "ActionTypes.hpp"
#include "HardwareFactory.hpp"
#include "magic_enum.hpp"
#include <filesystem>
#include <fstream>
#if defined(OMOTE_CONFIG_V2_PREVIEW) && !defined(IS_SIMULATOR)
#include "config_v2_logical_paths.hpp"
#include "config_v2_single_active_runtime.hpp"
#endif

namespace {
std::vector<std::filesystem::path> availableActionFiles() {
#if defined(OMOTE_CONFIG_V2_PREVIEW) && !defined(IS_SIMULATOR)
  const auto mode = config_v2_single_active_runtime::snapshot().mode;
  if (mode == config_v2_single_active_runtime::Mode::Recovery)
    return {};
  if (mode == config_v2_single_active_runtime::Mode::Active) {
    std::vector<std::filesystem::path> files;
    for (const auto &logical : config_v2_single_active_runtime::publicPaths()) {
      if (config_v2_logical_paths::inDirectory(logical, "Actions").empty())
        continue;
      const auto pinned = config_v2_single_active_runtime::resolveVfsPath(logical);
      if (!pinned.empty())
        files.emplace_back(pinned);
    }
    return files;
  }
#endif
  std::vector<std::filesystem::path> files;
  if (!std::filesystem::is_directory(ActionFactory::ActionsDirectory))
    return files;
  for (const auto &entry :
       std::filesystem::directory_iterator(ActionFactory::ActionsDirectory))
    if (entry.is_regular_file())
      files.push_back(entry.path());
  return files;
}
} // namespace

// TODO: Add Error tracking via stateful enum since we do not have std::expected in C++20
std::unique_ptr<IAction> ActionFactory::createAction(const rapidjson::Value &aProbableActionJson) {
  if (!mJsonValidator.IsAction(aProbableActionJson)) {
    mLastError = CreationError::InvalidActionJson;
    return nullptr;
  }
  auto &actionJson = aProbableActionJson;
  const std::string type = actionJson["type"].GetString();
  const std::string name = actionJson["name"].GetString();
  const auto &data = actionJson["data"];

  auto actionType = magic_enum::enum_cast<ActionTypes>(type).value_or(ActionTypes::COUNT);
  if (actionType == ActionTypes::COUNT) {
    mLastError = CreationError::UnknownActionType;
    return nullptr;
  }
  if (!mJsonValidator.IsDataValid(actionType, data)) {
    mLastError = CreationError::InvalidActionData;
    return nullptr;
  }
  auto &actionCreator = mActionCreators[static_cast<uint16_t>(actionType)];
  if (!actionCreator) {
    mLastError = CreationError::NoCreatorRegistered;
    return nullptr;
  }
  auto action = actionCreator(name, data);
  if (!action) {
    mLastError = CreationError::ActionCreationFailed;
    return nullptr;
  }

  return action;
}

std::unique_ptr<IAction> ActionFactory::createAction(const std::string &aActionName) {
  for (const auto &path : availableActionFiles()) {
    rapidjson::Document actionDoc = OMOTE::JSON::GetDocument(path);
    if (actionDoc.IsObject() && actionDoc.HasMember("name") &&
        actionDoc["name"].IsString() &&
        actionDoc["name"].GetString() == aActionName)
      return createAction(actionDoc);
  }
  return nullptr;
}

std::vector<std::unique_ptr<IAction>> ActionFactory::getAllActions() {
  std::vector<std::unique_ptr<IAction>> actions;

  for (const auto &path : availableActionFiles()) {
    auto actionDoc = OMOTE::JSON::GetDocument(path);
    auto action = createAction(actionDoc);
    if (action) {
      actions.push_back(std::move(action));
    }
  }
  return actions;
}
