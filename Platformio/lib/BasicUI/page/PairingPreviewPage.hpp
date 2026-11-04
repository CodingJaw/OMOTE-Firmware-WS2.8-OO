#pragma once
// Read-only local pairing UI preview. It CANNOT pair with HA or grant access.
// Added under OMOTE_CONFIG_V2_PREVIEW only; no network pairing handler exists.
#include "PageBase.hpp"
#include "config_v2_pairing_policy.hpp"
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
#include "config_v2_sec2_lab_transport.hpp"
#include "config_v2_pairing_attempt_budget.hpp"
#include "config_v2_sec2_lab_budget.hpp"
#endif
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
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
  void startEncryptedLabFromTouch();
#endif
  config_v2_pairing_policy::Window pairing_;
  Widget::Label *code_ = nullptr;
  Widget::Label *status_ = nullptr;
  Widget::Label *fingerprint_ = nullptr;
  Widget::Button *approve_ = nullptr;
  Widget::Button *reject_ = nullptr;
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
  config_v2_sec2_lab_transport::Probe labProbe_;
  config_v2_sec2_lab_budget::Store labAttemptStore_;
  config_v2_pairing_attempt_budget::Gate<config_v2_sec2_lab_budget::Store>
      labAttempts_{labAttemptStore_, 0};
  Widget::Button *labStart_ = nullptr;
#endif
  bool cancelled_ = false;
  lv_timer_t *timer_ = nullptr;
};
} // namespace UI::Page
