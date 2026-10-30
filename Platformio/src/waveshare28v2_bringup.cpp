// Stage 1 hardware bring-up for Waveshare ESP32-S3-Touch-LCD-2.8 V2.
// Intentionally separate from OMOTE's existing Rev1/Rev5 hardware setup:
// those revisions drive GPIOs that are not safe to assume on this board.
#include <Arduino.h>
#include <lvgl.h>
#include "Waveshare28V2.hpp"

static waveshare28v2::Board board;
static lv_obj_t *touchLabel = nullptr;
static lv_obj_t *tapLabel = nullptr;
static unsigned taps = 0;

static void onTap(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  ++taps;
  lv_label_set_text_fmt(tapLabel, "Button taps: %u", taps);
  Serial.printf("[waveshare28v2] LVGL button tap %u\n", taps);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("[waveshare28v2] OMOTE hardware bring-up stage 1");
  lv_init();
  if (!board.begin()) {
    Serial.println("[waveshare28v2] initialization failed");
    return;
  }

  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x10151c), 0);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "Waveshare 2.8 V2");
  lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);

  lv_obj_t *subtitle = lv_label_create(screen);
  lv_label_set_text(subtitle, "OMOTE hardware port - Stage 1");
  lv_obj_set_style_text_color(subtitle, lv_color_hex(0x9eb5c9), 0);
  lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 50);

  lv_obj_t *chip = lv_label_create(screen);
  lv_label_set_text(chip, board.touchDetected()
                             ? "ST7789 SPI / CST3530 detected"
                             : "ST7789 SPI / touch NOT detected");
  lv_obj_set_style_text_color(chip, lv_color_hex(0xffffff), 0);
  lv_obj_align(chip, LV_ALIGN_TOP_MID, 0, 90);

  lv_obj_t *button = lv_button_create(screen);
  lv_obj_set_size(button, 175, 65);
  lv_obj_align(button, LV_ALIGN_CENTER, 0, 10);
  lv_obj_add_event_cb(button, onTap, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *buttonLabel = lv_label_create(button);
  lv_label_set_text(buttonLabel, "Test touch");
  lv_obj_center(buttonLabel);

  tapLabel = lv_label_create(screen);
  lv_label_set_text(tapLabel, "Button taps: 0");
  lv_obj_align(tapLabel, LV_ALIGN_BOTTOM_MID, 0, -50);

  touchLabel = lv_label_create(screen);
  lv_label_set_text(touchLabel, "Touch: released");
  lv_obj_align(touchLabel, LV_ALIGN_BOTTOM_MID, 0, -20);

  Serial.println("[waveshare28v2] Touch the on-screen button to test");
}

void loop() {
  board.tick();
  if (touchLabel) {
    static uint32_t lastUpdate = 0;
    if (millis() - lastUpdate >= 100) {
      lastUpdate = millis();
      if (board.pressed()) {
        lv_label_set_text_fmt(touchLabel, "Touch: %u, %u",
                              board.touchX(), board.touchY());
      } else {
        lv_label_set_text(touchLabel, "Touch: released");
      }
    }
  }
  delay(5);
}
