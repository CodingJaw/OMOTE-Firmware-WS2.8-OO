#include "JsonDevices/KeyAction.hpp"
#include "ActionFactory.hpp"
#include "magic_enum.hpp"

// TODO replace manual validation with json schema validation
KeyAction::KeyAction(const rapidjson::Value &aKeyPressBehaviorJson) {
  // Legacy and active configuration files are not inherently trustworthy.
  // RapidJSON's HasMember/ObjectEmpty assert when called on non-objects.
  if (!aKeyPressBehaviorJson.IsObject())
    return;
  ActionFactory factory;
  for (auto KeyIdEnum : magic_enum::enum_values<KeyPressAbstract::KeyEvent::Type>()) {
    auto KeyIdStr = magic_enum::enum_name(KeyIdEnum);
    if (aKeyPressBehaviorJson.HasMember(KeyIdStr.data())) {
      const auto &actionJson = aKeyPressBehaviorJson[KeyIdStr.data()];
      if (actionJson.IsObject() && !actionJson.ObjectEmpty()) {
        auto action = factory.createAction(actionJson);
        if (action) {
          mActions[KeyIdEnum] = std::move(action);
        }
      }
    }
  }
}

bool KeyAction::isValid() const {
  return !mActions.empty();
}

void KeyAction::ExecuteAction(KeyPressAbstract::KeyEvent::Type aKeyEventType) {
  auto it = mActions.find(aKeyEventType);
  if (it != mActions.end()) {
    it->second->execute();
  }
}
