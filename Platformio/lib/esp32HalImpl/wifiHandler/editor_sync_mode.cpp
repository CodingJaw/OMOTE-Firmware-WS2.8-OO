#if !defined(IS_SIMULATOR)

#include "editor_sync_mode.hpp"

#include "HardwareFactory.hpp"
#include "device_settings.hpp"

#include <Arduino.h>
#include <WiFi.h>

namespace {

bool sActive = false;
bool sShowOverlay = false;
uint32_t sSavedSleepTimeout = 0;
uint32_t sSavedLightSleepTimeout = 0;
bool sSavedLightSleepEnabled = false;

} // namespace

namespace editor_sync_mode {

bool isActive() { return sActive; }

bool overlayRequested() { return sActive && sShowOverlay; }

bool enter(bool showOverlay) {
  if (sActive) {
    sShowOverlay = sShowOverlay || showOverlay;
    return true;
  }

  sActive = true;
  sShowOverlay = showOverlay;
  auto &hw = HardwareFactory::getAbstract();
  sSavedSleepTimeout = hw.getSleepTimeout();
  sSavedLightSleepTimeout = hw.getLightSleepTimeout();
  sSavedLightSleepEnabled = hw.getLightSleepEnabled();

  hw.setSleepTimeout(30UL * 60UL * 1000UL);
  hw.setLightSleepTimeout(0);
  hw.setLightSleepEnabled(false);

  if (auto *ir = hw.ir().get())
    ir->disableRx();

  device_settings::notifyActivity();
  if (auto disp = hw.display())
    disp->ensureTouchReady();

  return true;
}

void exit(bool reboot) {
  if (!sActive) {
    if (reboot)
      ESP.restart();
    return;
  }

  sActive = false;
  sShowOverlay = false;
  auto &hw = HardwareFactory::getAbstract();
  hw.setSleepTimeout(sSavedSleepTimeout ? sSavedSleepTimeout : 20000);
  hw.setLightSleepTimeout(sSavedLightSleepTimeout ? sSavedLightSleepTimeout : 60000);
  hw.setLightSleepEnabled(sSavedLightSleepEnabled);

  if (auto *ir = hw.ir().get())
    ir->enableRx();

  if (auto disp = hw.display())
    disp->ensureTouchReady();

  if (reboot) {
    device_settings::notifyActivity();
    if (auto disp = hw.display())
      disp->wake();
    delay(80);
    ESP.restart();
  }
}

} // namespace editor_sync_mode

#endif // !IS_SIMULATOR
