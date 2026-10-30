#include "WaveshareHardware.hpp"

#include "EspStats.hpp"
#include "keys.hpp"
#include "esp32WebSocket.hpp"
#include "wifihandler.hpp"
#include <Arduino.h>
#include <LittleFS.h>
#include <cstdarg>
#include <cstdio>
#include <vector>

namespace {

// Test-phase hardware stubs: no IR transmission or battery readings.
class NoBattery final : public BatteryInterface {
public:
  int getPercentage() override { return 0; }
  uint16_t getRawSOC() override { return 0; }
  int getVoltage() override { return 0; }
  bool isCharging() override { return false; }
  void writeCustomModel() override {}
  void disableHibernate(bool) override {}
  void saveLinearisationData(bool, uint16_t *, uint16_t, int, int) override {}
  void setCalMode(int) override {}
  int getCalMode() override { return 0; }
  std::vector<uint16_t> getCalData() override { return {}; }
};

class NoIR final : public IRInterface {
public:
  void send(int64SendTypes, uint64_t) override { unsupported(); }
  void send(int16SendTypes, std::vector<uint16_t> &, uint16_t) override { unsupported(); }
  void send(constInt64SendTypes, const uint64_t) override { unsupported(); }
  void send(charArrSendType, const unsigned char[]) override { unsupported(); }
  void send(RawIR) override { unsupported(); }
  void sendBackground(std::string, std::vector<std::string>) override { unsupported(); }
  int8_t calibrateTx() override { return 0; }
  void enableRx() override {}
  void disableRx() override {}
  void loopHandleRx() override {}
private:
  void unsupported() { Serial.println("[waveshare28v2] IR disabled: no emitter configured"); }
};

} // namespace

WaveshareDisplay::WaveshareDisplay() {
  if (!board_.begin()) {
    Serial.println("[waveshare28v2] Display or LVGL initialization failed");
  }
  mDisplay = board_.lvglDisplay();
  // OMOTE UIBase services the LVGL task handler. Supply an independent
  // millisecond clock rather than running the standalone Board::tick().
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
}

void WaveshareDisplay::updateBrightness() {
  board_.setBacklight(isDay_ ? day_ : night_);
}

void WaveshareDisplay::setLcdDayBrightness(uint8_t value, bool) {
  day_ = value;
  updateBrightness();
}
void WaveshareDisplay::setLcdNightBrightness(uint8_t value, bool) {
  night_ = value;
  updateBrightness();
}
void WaveshareDisplay::setKbdDayBrightness(uint8_t value, bool) { kbdDay_ = value; }
void WaveshareDisplay::setKbdNightBrightness(uint8_t value, bool) { kbdNight_ = value; }
void WaveshareDisplay::initBrightnessLevels(uint8_t day, uint8_t night,
                                           uint8_t kbdDay, uint8_t kbdNight) {
  day_ = day;
  night_ = night;
  kbdDay_ = kbdDay;
  kbdNight_ = kbdNight;
  updateBrightness();
}
void WaveshareDisplay::startFade(uint16_t) { updateBrightness(); }
void WaveshareDisplay::turnOff() { board_.setBacklight(0); }
void WaveshareDisplay::setDayMode(bool day) {
  isDay_ = day;
  updateBrightness();
}
void WaveshareDisplay::getTouchData() {
  // Polling is owned by Waveshare28V2::Board's LVGL pointer callback.
}

void WaveshareHardware::init() {
  startMs_ = millis();
  Serial.begin(115200);
  Serial.println("[waveshare28v2] Starting real OMOTE JsonUI");

  // Do not silently erase flash if LittleFS was not uploaded.
  if (!LittleFS.begin(false)) {
    Serial.println("[waveshare28v2] LittleFS not mounted; run uploadfs for JsonUI scenes");
  } else {
    Serial.printf("[waveshare28v2] LittleFS mounted; Scenes.json: %s\n",
                  LittleFS.exists("/Scenes.json") ? "found" : "missing (run uploadfs)");
  }

  display_ = std::make_shared<WaveshareDisplay>();
  keys_ = std::make_shared<Keys>();
  battery_ = std::make_shared<NoBattery>();
  ir_ = std::make_shared<NoIR>();
  stats_ = std::make_shared<EspStats>();
  auto handler = wifiHandler::getInstance();
  wifi_ = handler;
  handler->mqttRestoreCredentials();
  handler->setupMqttBroker();
  handler->ntpRestoreCredentials();
  handler->setupNtp();
  handler->ftpRestoreCredentials();

  Serial.println("[waveshare28v2] Hardware interfaces ready; keypad/IR/IMU/sleep disabled");
}

void WaveshareHardware::loopHandler() {
  const uint32_t now = millis();
  if (now - lastNetworkPoll_ >= 50) {
    lastNetworkPoll_ = now;
    if (wifi_) {
      wifi_->networkSync();
      wifi_->mqttSync();
    }
  }
}

void WaveshareHardware::debugPrint(const char *fmt, ...) {
  va_list arguments;
  va_start(arguments, fmt);
  vprintf(fmt, arguments);
  va_end(arguments);
}

std::unique_ptr<LoggingInterface> WaveshareHardware::logger() {
  return std::make_unique<LoggingInterface>();
}

std::shared_ptr<webSocketInterface> WaveshareHardware::webSocket() {
  return std::make_shared<esp32WebSocket>(
      std::static_pointer_cast<wifiHandler>(wifi_), logger());
}
