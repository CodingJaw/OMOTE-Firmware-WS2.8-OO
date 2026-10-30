#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <lvgl.h>

// Verified on CodingJaw/lvgl-wsesp32s32.8 (waveshare-v2-showcase).
// This is the Waveshare 2.8-inch V2 board, NOT the older CST328 revision.
namespace waveshare28v2 {

constexpr uint16_t kWidth = 240;
constexpr uint16_t kHeight = 320;

constexpr int kLcdSck = 40;
constexpr int kLcdMosi = 45;
constexpr int kLcdCs = 42;
constexpr int kLcdDc = 41;
constexpr int kLcdReset = 39;
constexpr int kBacklight = 5;

constexpr int kTouchSda = 1;
constexpr int kTouchScl = 3;
constexpr int kTouchReset = 2;
constexpr int kTouchInterrupt = 4;
constexpr uint8_t kTouchAddress = 0x58;

class Board {
public:
  // Initializes display, CST3530 touch and LVGL's display/input devices.
  // Call lv_init() before begin().
  bool begin();
  void tick(); // Call regularly from the Arduino loop.
  lv_display_t *lvglDisplay() const { return display_; }
  void setBacklight(uint8_t duty);
  bool touchDetected() const { return touchDetected_; }
  bool pressed() const { return pressed_; }
  uint16_t touchX() const { return touchX_; }
  uint16_t touchY() const { return touchY_; }

private:
  static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels);
  static void readPointer(lv_indev_t *indev, lv_indev_data_t *data);
  bool sendTouchCommand(uint32_t command, bool stop = true);
  bool readTouchFrame(uint8_t frame[9]);

  static Board *active_;
  Arduino_ESP32SPI spi_{kLcdDc, kLcdCs, kLcdSck, kLcdMosi, GFX_NOT_DEFINED};
  Arduino_ST7789 lcd_{&spi_, kLcdReset, 0, true, kWidth, kHeight};
  lv_display_t *display_ = nullptr;
  lv_indev_t *pointer_ = nullptr;
  // 24 scanlines; partial buffers keep DMA/heap demands modest.
  uint16_t drawBuffer_[kWidth * 24] = {};
  uint32_t lastTick_ = 0;
  uint32_t lastTouchPoll_ = 0;
  uint16_t touchX_ = 0;
  uint16_t touchY_ = 0;
  bool pressed_ = false;
  bool touchDetected_ = false;
};

} // namespace waveshare28v2
