#pragma once
// Read-only local pairing UI preview. It CANNOT pair with HA or grant access.
// Added under OMOTE_CONFIG_V2_PREVIEW only; no network pairing handler exists.
#include "PageBase.hpp"
#include "config_v2_pairing_policy.hpp"
#include <lvgl.h>

namespace UI::Widget { class Label; class Button; }

namespace UI::Page {
class PairingPreviewPage final : public Base {
public:
  explicit PairingPreviewPage(config_v2_pairing_policy::Entry entry);
  ~PairingPreviewPage() override;
  std::string GetTitle() override { return "HA pairing preview"; }

private:
  static void timerTick(lv_timer_t *timer);
  void refresh();
  void approveFromTouch();
  void rejectFromTouch();
  config_v2_pairing_policy::Window pairing_;
  Widget::Label *code_ = nullptr;
  Widget::Label *status_ = nullptr;
  Widget::Label *fingerprint_ = nullptr;
  Widget::Button *approve_ = nullptr;
  Widget::Button *reject_ = nullptr;
  bool cancelled_ = false;
  lv_timer_t *timer_ = nullptr;
};
} // namespace UI::Page
