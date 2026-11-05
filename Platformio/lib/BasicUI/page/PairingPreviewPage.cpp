#if defined(OMOTE_CONFIG_V2_PREVIEW) && defined(OMOTE_WAVESHARE28V2) && !defined(IS_SIMULATOR)
#include "PairingPreviewPage.hpp"
#include "Button.hpp"
#include "Label.hpp"
#include <Arduino.h>
#include <esp_system.h>
#include <string>
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
#include <WiFi.h>
#endif

using namespace UI::Page;

PairingPreviewPage::PairingPreviewPage(config_v2_pairing_policy::Entry entry)
    : Base(ID::Pages::Settings) {
  // LOCAL-ONLY preview. No socket, REST, MQTT or BLE endpoint can supply a
  // VerifiedPeer, so the Approve action cannot currently grant trust.
  const auto started = pairing_.begin(entry, millis(), []() -> uint32_t {
    return esp_random();
  });

#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
  auto *heading = AddNewElement<UI::Widget::Label>("SECURITY2 LAB - NO TRUST");
#else
  auto *heading = AddNewElement<UI::Widget::Label>("HA PAIRING - PREVIEW ONLY");
#endif
  heading->SetWidth(208);
  heading->SetHeight(20);
  heading->AlignTo(this, LV_ALIGN_TOP_MID, 0, 8);

  code_ = AddNewElement<UI::Widget::Label>(
      started == config_v2_pairing_policy::Status::Ok
          ? pairing_.codeForLocalScreen(millis()) : "Unavailable");
  code_->SetWidth(208);
  code_->SetHeight(32);
  code_->AlignTo(heading, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

  fingerprint_ = AddNewElement<UI::Widget::Label>("");
  fingerprint_->SetWidth(208);
  fingerprint_->SetHeight(72);
  fingerprint_->AlignTo(code_, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
  lv_label_set_long_mode(fingerprint_->LvglSelf(), LV_LABEL_LONG_WRAP);

  status_ = AddNewElement<UI::Widget::Label>(
      "This code cannot pair with HA yet.\n"
      "Secure session and HA identity\n"
      "verification are not connected.\n"
      "Device writes are disabled.");
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
  status_->SetText(
      "Local-only encrypted test.\n"
      "Tap Start lab to open the\n"
      "short-lived lab listener.\n"
      "NOT a trusted pairing.");
#endif
  status_->SetWidth(208);
  status_->SetHeight(72);
  status_->AlignTo(fingerprint_, LV_ALIGN_OUT_BOTTOM_MID, 0, 3);
  lv_label_set_long_mode(status_->LvglSelf(), LV_LABEL_LONG_WRAP);

  // Both callbacks originate ONLY from physical LVGL button gestures.
  // The policy additionally refuses to approve without a secure peer.
  approve_ = AddNewElement<UI::Widget::Button>(
      [this] { approveFromTouch(); });
  approve_->SetText("Approve");
  approve_->SetSize(97, 34);
  approve_->AlignTo(this, LV_ALIGN_BOTTOM_LEFT, 6, -6);
  lv_obj_add_flag(approve_->LvglSelf(), LV_OBJ_FLAG_HIDDEN);

  reject_ = AddNewElement<UI::Widget::Button>(
      [this] { rejectFromTouch(); });
  reject_->SetText("Cancel");
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
  // This is a genuine LVGL touch callback on the physical CST3530 display.
  // No REST, MQTT or serial path can execute it.
  labStart_ = AddNewElement<UI::Widget::Button>(
      [this] { startEncryptedLabFromTouch(); });
  labStart_->SetText("Start lab");
  labStart_->SetSize(97, 34);
  labStart_->AlignTo(this, LV_ALIGN_BOTTOM_LEFT, 6, -6);
#endif
  reject_->SetSize(97, 34);
  reject_->AlignTo(this, LV_ALIGN_BOTTOM_RIGHT, -6, -6);

  timer_ = lv_timer_create(timerTick, 500, this);
}

PairingPreviewPage::~PairingPreviewPage() {
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
  labProbe_.stop();
#endif
  if (timer_) {
    lv_timer_del(timer_);
    timer_ = nullptr;
  }
  pairing_.cancel();
}

void PairingPreviewPage::timerTick(lv_timer_t *timer) {
  auto *self = static_cast<PairingPreviewPage *>(lv_timer_get_user_data(timer));
  if (self) self->refresh();
}

void PairingPreviewPage::approveFromTouch() {
  // Future secure PAKE work must bind this page to a trusted coordinator;
  // this preview Window has no network entry point and cannot store trust.
  const auto now = millis();
  if (!pairing_.peerForLocalScreen(now) ||
      pairing_.confirmTouch(now) != config_v2_pairing_policy::Status::Ok) {
    if (status_) status_->SetText("No verified peer. Pairing unavailable.");
    return;
  }
  // Even a local approval is NOT durable pairing until the coordinator has
  // revalidated the secure session and persisted the approved fingerprint.
  if (status_) status_->SetText(
      "Local touch confirmed.\n"
      "Trust is NOT stored.\n"
      "Encrypted pairing unavailable.");
  refresh();
}

void PairingPreviewPage::rejectFromTouch() {
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
  labProbe_.stop();
  if (labStart_) lv_obj_add_flag(labStart_->LvglSelf(), LV_OBJ_FLAG_HIDDEN);
#endif
  pairing_.cancel();
  cancelled_ = true;
  if (code_) code_->SetText("Cancelled");
  if (status_) status_->SetText("Pairing preview cancelled.\nNo settings changed.");
  if (fingerprint_) fingerprint_->SetText("");
  if (approve_) lv_obj_add_flag(approve_->LvglSelf(), LV_OBJ_FLAG_HIDDEN);
  if (reject_) reject_->SetText("Closed");
}

void PairingPreviewPage::refresh() {
  const auto now = millis();
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
  labProbe_.tick(now);
#endif
  const auto phase = pairing_.phase(now);
  if (phase == config_v2_pairing_policy::Phase::AwaitingTouch) {
    const auto *peer = pairing_.peerForLocalScreen(now);
    if (!peer) return;
    // Show the FULL verified peer fingerprint so the operator can compare
    // identity. The transport must authenticate this fingerprint first.
    const std::string &fp = peer->publicKeyFingerprint;
    const std::string readable = "HA peer SHA-256:\n" +
        fp.substr(0, 16) + "\n" + fp.substr(16, 16) + "\n" +
        fp.substr(32, 16) + "\n" + fp.substr(48, 16);
    if (fingerprint_) fingerprint_->SetText(readable);
    if (code_) code_->SetText("Peer authenticated");
    if (status_) status_->SetText(
        "Compare HA identity. This preview\n"
        "cannot store pairing trust.");
    if (approve_) lv_obj_remove_flag(approve_->LvglSelf(), LV_OBJ_FLAG_HIDDEN);
    if (reject_) reject_->SetText("Reject");
  } else {
    if (approve_) lv_obj_add_flag(approve_->LvglSelf(), LV_OBJ_FLAG_HIDDEN);
    if (phase == config_v2_pairing_policy::Phase::Idle && !cancelled_) {
      if (code_) code_->SetText("Expired");
      if (status_) status_->SetText(
          "Pairing preview expired.\nNo settings changed.");
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
      labProbe_.stop();
      if (labStart_) {
        labStart_->SetText("Lab expired");
        lv_obj_add_state(labStart_->LvglSelf(), LV_STATE_DISABLED);
      }
#endif
    }
  }
}

#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
void PairingPreviewPage::startEncryptedLabFromTouch() {
  // A local Settings touchscreen tap is required for every lab window.
  // Reserve nonvolatile attempt budget *before* opening any network socket.
  // This counts lab window openings, not individual SRP attempts: never
  // deploy this as production pairing or claim brute-force protection.
  if (cancelled_ || labProbe_.active() ||
      pairing_.phase(millis()) != config_v2_pairing_policy::Phase::ShowCode)
    return;
  if (!WiFi.isConnected()) {
    if (status_) status_->SetText("Connect Wi-Fi before starting the lab.");
    return;
  }
  const std::string pin = pairing_.codeForLocalScreen(millis());
  if (pin.size() != 6) return;
  const auto reservation = labAttempts_.reserve(millis());
  if (reservation != config_v2_pairing_attempt_budget::Status::Allowed) {
    if (status_) status_->SetText(
        "Lab attempt budget exhausted/invalid.\n"
        "No network listener opened.\n"
        "Do not erase NVS remotely.");
    if (labStart_) {
      labStart_->SetText("Lab locked");
      lv_obj_add_state(labStart_->LvglSelf(), LV_STATE_DISABLED);
    }
    return;
  }
  const bool started = labProbe_.startFromTrustedLocalTouch(
      pin.data(), pin.size(), millis());
  if (!started) {
    if (status_) status_->SetText(
        "Security2 lab listener failed.\n"
        "No trust or config changed.");
    if (labStart_) {
      labStart_->SetText("Start failed");
      lv_obj_add_state(labStart_->LvglSelf(), LV_STATE_DISABLED);
    }
    return;
  }
  if (status_) status_->SetText(
      "Encrypted LAB test port 18082.\n"
      "Use official client + screen PIN.\n"
      "Expires in 120 seconds.\n"
      "NO PAIRING, NO DEPLOYMENT.");
  // Preserve visible feedback after activation. Previously this button was
  // hidden immediately, leaving no visible indication that Start lab worked.
  // Disabling it prevents duplicate listener opens or additional NVS charges.
  if (labStart_) {
    labStart_->SetText("Lab active");
    lv_obj_add_state(labStart_->LvglSelf(), LV_STATE_DISABLED);
  }
}
#endif
#endif // Waveshare 2.8 V2 local-only pairing preview
