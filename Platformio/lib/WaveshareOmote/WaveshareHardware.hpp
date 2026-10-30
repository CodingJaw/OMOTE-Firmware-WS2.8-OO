#pragma once

#include "HardwareAbstract.hpp"
#include "Waveshare28V2.hpp"
#include <memory>

// No original OMOTE Rev1/Rev5 pins are initialized by this hardware adapter.
// Keypad, IR, battery gauge, motion sensor and sleep are pending external wiring.
class WaveshareDisplay : public DisplayAbstract {
public:
  WaveshareDisplay();
  void setLcdDayBrightness(uint8_t brightness, bool instant = false) override;
  void setLcdNightBrightness(uint8_t brightness, bool instant = false) override;
  void setKbdDayBrightness(uint8_t brightness, bool instant = false) override;
  void setKbdNightBrightness(uint8_t brightness, bool instant = false) override;
  uint8_t getLcdDayBrightness() override { return day_; }
  uint8_t getLcdNightBrightness() override { return night_; }
  uint8_t getKbdDayBrightness() override { return kbdDay_; }
  uint8_t getKbdNightBrightness() override { return kbdNight_; }
  void initBrightnessLevels(uint8_t day, uint8_t night,
                            uint8_t kbdDay, uint8_t kbdNight) override;
  void startFade(uint16_t delay) override;
  void turnOff() override;
  void setDayMode(bool day) override;
  void getTouchData() override;  // CST3530 is polled in LVGL read callback.
  void wake() override;
  void pokeTouchController() override; // LVGL owns CST3530 polling
  void ensureTouchReady() override;   // CST3530 stays initialized on this board

private:
  void updateBrightness();
  waveshare28v2::Board board_;
  uint8_t day_ = 128;
  uint8_t night_ = 50;
  uint8_t kbdDay_ = 0;
  uint8_t kbdNight_ = 0;
  bool isDay_ = true;
};

class WaveshareHardware : public HardwareAbstract {
public:
  void init() override;
  void loopHandler() override;
  void debugPrint(const char *fmt, ...) override;

  std::unique_ptr<LoggingInterface> logger() override;
  std::shared_ptr<BatteryInterface> battery() override { return battery_; }
  std::shared_ptr<DisplayAbstract> display() override { return display_; }
  std::shared_ptr<wifiHandlerInterface> wifi() override { return wifi_; }
  std::shared_ptr<KeyPressAbstract> keys() override { return keys_; }
  std::shared_ptr<IRInterface> ir() override { return ir_; }
  std::shared_ptr<SystemStatsInterface> stats() override { return stats_; }
  std::shared_ptr<webSocketInterface> webSocket() override;

  std::chrono::milliseconds execTime() override {
    return std::chrono::milliseconds(millis() - startMs_);
  }

  char getCurrentDevice() override { return currentDevice_; }
  void setCurrentDevice(char value) override { currentDevice_ = value; }
  bool getWakeupByIMUEnabled() override { return false; }
  void setWakeupByIMUEnabled(bool) override {}
  uint32_t getSleepTimeout() override { return sleepTimeout_; }
  void setSleepTimeout(uint32_t value) override { sleepTimeout_ = value; }
  bool getLightSleepEnabled() override { return false; }
  void setLightSleepEnabled(bool) override {}
  uint32_t getLightSleepTimeout() override { return 0; }
  void setLightSleepTimeout(uint32_t) override {}
  void saveSettings() override {}
  void setInScene(bool) override {}
  void enterSleep(SleepMode, uint32_t) override {} // Disabled pending hardware validation.
  WakeReason getWakeUpReason() override { return WakeReason::RESET; }
  unsigned long getMillis() override { return millis(); }
  bool isUsbConnected() override { return true; }
  unsigned long getWakeTime() override { return startMs_; }

private:
  unsigned long startMs_ = 0;
  uint32_t lastNetworkPoll_ = 0;
  uint32_t sleepTimeout_ = 20000;
  char currentDevice_ = 1;
  std::shared_ptr<BatteryInterface> battery_;
  std::shared_ptr<WaveshareDisplay> display_;
  std::shared_ptr<wifiHandlerInterface> wifi_;
  std::shared_ptr<KeyPressAbstract> keys_;
  std::shared_ptr<IRInterface> ir_;
  std::shared_ptr<SystemStatsInterface> stats_;
};
