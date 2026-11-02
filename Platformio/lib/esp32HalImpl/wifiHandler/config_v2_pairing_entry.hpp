#pragma once
// Physical keypad-chord detector independent of keyboard GPIO/TCA8418 wiring.
// Chosen preliminary chord: HOME + BACK held together until either produces
// the firmware's Long event. A KeyEvent must originate from a REAL local
// keypad driver, never an HTTP/MQTT/BLE injected or simulated remote command.
// No network route may invoke this detector as proof of physical presence.
namespace config_v2_pairing_entry {
enum class Button { Home, Back, Other };
enum class Event { Press, Release, Long, Other };
class PhysicalChord {
public:
  bool observe(Button button, Event event) {
    if (event == Event::Release) {
      if (button == Button::Home) home_ = false;
      if (button == Button::Back) back_ = false;
      if (!home_ || !back_) latched_ = false;
      return false;
    }
    if (event == Event::Press) {
      if (button == Button::Home) home_ = true;
      if (button == Button::Back) back_ = true;
      return false;
    }
    if (event != Event::Long || latched_)
      return false;
    if ((button == Button::Home || button == Button::Back) &&
        home_ && back_) {
      latched_ = true;
      return true;
    }
    return false;
  }
  void clear() { home_ = back_ = latched_ = false; }
private:
  bool home_ = false;
  bool back_ = false;
  bool latched_ = false;
};
} // namespace config_v2_pairing_entry
