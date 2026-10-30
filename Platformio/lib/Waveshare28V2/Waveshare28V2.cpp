#include "Waveshare28V2.hpp"

namespace waveshare28v2 {

Board *Board::active_ = nullptr;

bool Board::sendTouchCommand(uint32_t command, bool stop) {
  Wire1.beginTransmission(kTouchAddress);
  for (int shift = 24; shift >= 0; shift -= 8) {
    Wire1.write(static_cast<uint8_t>(command >> shift));
  }
  return Wire1.endTransmission(stop) == 0;
}

bool Board::readTouchFrame(uint8_t frame[9]) {
  // CST3530 V2 protocol from the confirmed Waveshare swipe-test.
  bool ok = sendTouchCommand(0xd0070000, false);
  if (ok) {
    ok = Wire1.requestFrom(kTouchAddress, static_cast<size_t>(9)) == 9;
  }
  if (ok) {
    for (int i = 0; i < 9; ++i) {
      frame[i] = static_cast<uint8_t>(Wire1.read());
    }
  }
  const bool acknowledged = sendTouchCommand(0xd00002ab);
  return ok && acknowledged;
}

void Board::flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
  Board *self = active_;
  if (!self) {
    lv_display_flush_ready(display);
    return;
  }
  const int w = area->x2 - area->x1 + 1;
  const int h = area->y2 - area->y1 + 1;
  self->lcd_.draw16bitRGBBitmap(area->x1, area->y1,
                               reinterpret_cast<uint16_t *>(pixels), w, h);
  lv_display_flush_ready(display);
}

void Board::readPointer(lv_indev_t *, lv_indev_data_t *data) {
  Board *self = active_;
  if (!self) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  const uint32_t now = millis();
  if (now - self->lastTouchPoll_ >= 20) {
    self->lastTouchPoll_ = now;
    uint8_t frame[9] = {};
    const bool ok = self->touchDetected_ && self->readTouchFrame(frame);
    const uint8_t count = frame[3] & 0x0f;
    const uint16_t x = static_cast<uint16_t>(frame[4]) |
                       (static_cast<uint16_t>(frame[7] & 0x0f) << 8);
    const uint16_t y = static_cast<uint16_t>(frame[5]) |
                       (static_cast<uint16_t>(frame[7] & 0xf0) << 4);
    self->pressed_ = ok && count > 0 && count <= 5 &&
                     (frame[8] & 0xf0) != 0 && x < kWidth && y < kHeight;
    if (self->pressed_) {
      self->touchX_ = x;
      self->touchY_ = y;
    }
  }

  data->point.x = self->touchX_;
  data->point.y = self->touchY_;
  data->state = self->pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

bool Board::begin() {
  active_ = this;

  pinMode(kBacklight, OUTPUT);
  digitalWrite(kBacklight, LOW);

  if (!lcd_.begin(40000000)) {
    Serial.println("[waveshare28v2] ST7789 initialization failed");
    return false;
  }
  lcd_.fillScreen(0x0000);

  ledcAttach(kBacklight, 20000, 8);
  ledcWrite(kBacklight, 128);

  Wire1.begin(kTouchSda, kTouchScl, 400000);
  Wire1.setTimeOut(50);
  pinMode(kTouchInterrupt, INPUT);
  pinMode(kTouchReset, OUTPUT);
  digitalWrite(kTouchReset, LOW);
  delay(100);
  digitalWrite(kTouchReset, HIGH);
  delay(500);

  Wire1.beginTransmission(kTouchAddress);
  touchDetected_ = Wire1.endTransmission() == 0;
  Serial.printf("[waveshare28v2] CST3530: %s\n",
                touchDetected_ ? "detected" : "not detected");

  display_ = lv_display_create(kWidth, kHeight);
  if (!display_) {
    Serial.println("[waveshare28v2] LVGL display allocation failed");
    return false;
  }
  lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display_, flush);
  lv_display_set_buffers(display_, drawBuffer_, nullptr,
                         sizeof(drawBuffer_), LV_DISPLAY_RENDER_MODE_PARTIAL);

  pointer_ = lv_indev_create();
  lv_indev_set_type(pointer_, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(pointer_, readPointer);
  lastTick_ = millis();
  lastTouchPoll_ = millis() - 20;

  Serial.println("[waveshare28v2] ST7789 + LVGL ready");
  return true;
}

void Board::setBacklight(uint8_t duty) {
  ledcWrite(kBacklight, duty);
}

void Board::tick() {
  const uint32_t now = millis();
  lv_tick_inc(now - lastTick_);
  lastTick_ = now;
  lv_timer_handler();
}

} // namespace waveshare28v2
