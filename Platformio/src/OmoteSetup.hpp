#pragma once
#include "HardwareFactory.hpp"
#include "HomeAssistUI.hpp"
#include "JsonUI.hpp"
#ifdef OMOTE_WAVESHARE28V2
#include "WaveshareHardware.hpp"
#include <Arduino.h>
#include <esp_system.h>
#endif
#ifdef IS_SIMULATOR
#include <cstdio>
#endif
#ifdef OMOTE_HARDWARE_REV5
#include "Arduino.h"
#endif

namespace OMOTE {
std::shared_ptr<UI::UIBase> ui = nullptr;

#ifdef OMOTE_WAVESHARE28V2
// No scene manifest: keep LVGL, Wi-Fi and the AP alive instead of entering
// JsonUI, which depends on JSON files. Never erase/format flash automatically.
void showWaveshareStorageRecovery() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x142130), 0);

  lv_obj_t *title = lv_label_create(screen);
  lv_obj_set_width(title, 220);
  lv_label_set_text(title, "OMOTE: storage missing");
  lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 32);

  lv_obj_t *instructions = lv_label_create(screen);
  lv_obj_set_width(instructions, 220);
  lv_label_set_long_mode(instructions, LV_LABEL_LONG_WRAP);
  lv_label_set_text(instructions,
      "LittleFS not mounted or\n"
      "Scenes.json is missing.\n\n"
      "Upload OMOTE's data folder\n"
      "with the matching target:\n"
      "waveshare28v2_omote\n"
      "uploadfs\n\n"
      "Wi-Fi service remains active.");
  lv_obj_set_style_text_color(instructions, lv_color_hex(0xc7d7e7), 0);
  lv_obj_align(instructions, LV_ALIGN_TOP_MID, 0, 85);
}
#endif

void createUI() {
  // ui = std::make_unique<UI::BasicUI>();
  ui = std::make_unique<UI::JsonUI>();
  //ui = std::make_unique<UI::HomeAssistUI>();
  ui->restore();
}

void setup() {
#ifdef OMOTE_WAVESHARE28V2
  Serial.begin(115200);
  Serial.printf("[waveshare28v2] setup: reset reason=%d\n",
                static_cast<int>(esp_reset_reason()));
#endif
#ifdef OMOTE_HARDWARE_REV5
  // Safety check to make sure hardware matches software build and protect IR LED
  // GPIO3 is not connected on Rev5 and pulled up on Rev5_3661
  // If level is wrong go into deep sleep
  pinMode(3, INPUT_PULLDOWN);
  delay(5);
#ifdef OMOTE_KEYBRD_3661
  if (LOW == digitalRead(3)) {
    pinMode(3, INPUT); // prevent increased current due to external pull up resistor
    esp_deep_sleep_start();
  }
  pinMode(3, INPUT);
#else
  if (HIGH == digitalRead(3))
    esp_deep_sleep_start();
#endif
#endif

  lv_init();

  HardwareFactory::Init();
#ifdef OMOTE_WAVESHARE28V2
  Serial.println("[waveshare28v2] setup: hardware init returned");
#endif
  HardwareFactory::getAbstract().wifi()->begin();
#ifdef OMOTE_WAVESHARE28V2
  Serial.println("[waveshare28v2] setup: Wi-Fi begin returned");
  auto &boardHardware =
      static_cast<WaveshareHardware &>(HardwareFactory::getAbstract());
  if (!boardHardware.sceneFilesystemReady()) {
    Serial.println("[waveshare28v2] Missing scene filesystem; entering recovery UI (no reboot)");
    showWaveshareStorageRecovery();
    lv_timer_handler();
    return;
  }
  Serial.println("[waveshare28v2] setup: restoring JsonUI");
#endif
  createUI();
#ifdef OMOTE_WAVESHARE28V2
  Serial.println("[waveshare28v2] setup: JsonUI restored");
#endif
#ifdef IS_SIMULATOR
  std::fprintf(stderr, "[sim] setup: UI created, running first LVGL tick\n");
  std::fflush(stderr);
#endif
  lv_timer_handler(); // Run the LVGL UI once before the loop takes over
#ifdef IS_SIMULATOR
  std::fprintf(stderr, "[sim] setup complete — sim running (close SDL window only hides UI; process stays in terminal)\n");
  std::fflush(stderr);
#endif
}

void loop() {
  HardwareFactory::getAbstract().loopHandler();
  if (ui) {
    ui->loopHandler();
#ifdef OMOTE_WAVESHARE28V2
  } else {
    // Keep the recovery page and captive portal alive.
    lv_timer_handler();
    delay(5);
#endif
  }
}

} // namespace OMOTE
