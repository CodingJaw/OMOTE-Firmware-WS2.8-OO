#if defined(OMOTE_CONFIG_V2_PREVIEW) && defined(OMOTE_WAVESHARE28V2) && !defined(IS_SIMULATOR)
#include "PairingPreviewPage.hpp"
#include "Label.hpp"
#include <Arduino.h>
#include <esp_system.h>
#include <cstdio>

using namespace UI::Page;

PairingPreviewPage::PairingPreviewPage(config_v2_pairing_policy::Entry entry)
    : Base(ID::Pages::Settings) {
  // The code is strictly local, random and short-lived; no API accepts it yet.
  // Pairing must remain unavailable until a trusted encrypted provisional
  // channel and verified peer public key are implemented and tested.
  const auto started = pairing_.begin(entry, millis(), []() -> uint32_t {
    return esp_random();
  });

  auto *heading = AddNewElement<UI::Widget::Label>(
      "LOCAL PREVIEW ONLY");
  heading->SetWidth(205);
  heading->AlignTo(this, LV_ALIGN_TOP_MID, 0, 12);

  code_ = AddNewElement<UI::Widget::Label>(
      started == config_v2_pairing_policy::Status::Ok
          ? pairing_.codeForLocalScreen(millis()) : "Unavailable");
  code_->SetWidth(205);
  code_->AlignTo(heading, LV_ALIGN_OUT_BOTTOM_MID, 0, 18);

  status_ = AddNewElement<UI::Widget::Label>(
      "This code cannot pair with HA yet.\n"
      "Secure encrypted pairing and\n"
      "touch confirmation are not active.\n"
      "Remote writes remain disabled.");
  status_->SetWidth(205);
  status_->AlignTo(code_, LV_ALIGN_OUT_BOTTOM_MID, 0, 18);
  lv_label_set_long_mode(status_->LvglSelf(), LV_LABEL_LONG_WRAP);

  timer_ = lv_timer_create(timerTick, 500, this);
}

PairingPreviewPage::~PairingPreviewPage() {
  if (timer_) {
    lv_timer_del(timer_);
    timer_ = nullptr;
  }
  pairing_.cancel();
}

void PairingPreviewPage::timerTick(lv_timer_t *timer) {
  auto *self = static_cast<PairingPreviewPage *>(lv_timer_get_user_data(timer));
  if (self)
    self->refresh();
}

void PairingPreviewPage::refresh() {
  if (pairing_.phase(millis()) == config_v2_pairing_policy::Phase::Idle) {
    if (code_)
      code_->SetText("Expired");
  }
}

#endif // Waveshare 2.8 V2 Config v2 local preview
