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
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
  approve_->SetText("Review OK");
#else
  approve_->SetText("Approve");
#endif
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
      [this] {
        if (labLocked_) rearmEncryptedLabFromTouch();
        else startEncryptedLabFromTouch();
      });
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
#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)
  // This is ONLY an acknowledgement of proof evidence in volatile RAM.
  // It neither persists HA trust nor authorizes configuration commands.
  const auto reviewed = labProbe_.acknowledgeLabReviewFromLocalTouch(millis());
  if (reviewed == config_v2_sec2_lab_mutual_review::Result::LocalReviewOnly) {
    if (status_) status_->SetText(
        "LAB evidence reviewed.\n"
        "NO HA trust was stored.\n"
        "NO pairing or deployment.\n"
        "Close this test window.");
    if (approve_) lv_obj_add_flag(approve_->LvglSelf(), LV_OBJ_FLAG_HIDDEN);
    if (reject_) reject_->SetText("Close");
    return;
  }
#endif
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
  labProbe_.rejectLabReviewFromLocalTouch();
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
  // A locked lab must remain locally rearmable after the short PIN page
  // expires. No timer, retry or reboot is permitted to clear its quota.
  if (labLocked_ || labRearmed_ || labStorageError_) return;
  if (labProbe_.active()) {
    const auto review = labProbe_.labReviewSnapshot(now);
    if (review.state ==
        config_v2_sec2_lab_mutual_review::State::AwaitingTouch) {
      // Show the FULL SHA-256 candidate HA fingerprint in four 16-character
      // rows on the physical touchscreen. This is a LAB-only comparison,
      // not a completed trusted pairing or authorization.
      if (code_) code_->SetText("HA key proof verified");
      const auto &fp = review.haFingerprint;
      if (fingerprint_) fingerprint_->SetText(
          fp.substr(0, 16) + "\n" + fp.substr(16, 16) +
          "\n" + fp.substr(32, 16) + "\n" + fp.substr(48, 16));
      if (status_) status_->SetText(
          "Both LAB proofs verified.\n"
          "Compare HA fingerprint.\n"
          "Review OK or Reject.\n"
          "NO TRUST, NO DEPLOYMENT.");
      if (approve_) lv_obj_remove_flag(approve_->LvglSelf(), LV_OBJ_FLAG_HIDDEN);
      if (reject_) reject_->SetText("Reject");
      return;
    }
    if (review.state == config_v2_sec2_lab_mutual_review::State::Reviewed) {
      if (approve_) lv_obj_add_flag(approve_->LvglSelf(), LV_OBJ_FLAG_HIDDEN);
      return;
    }
    if (review.state == config_v2_sec2_lab_mutual_review::State::Expired) {
      if (status_) status_->SetText(
          "LAB peer review expired.\n"
          "No trust stored.\n"
          "Close the test window.");
      if (approve_) lv_obj_add_flag(approve_->LvglSelf(), LV_OBJ_FLAG_HIDDEN);
      return;
    }
  }
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
void PairingPreviewPage::showLabLockout() {
  labLocked_ = true;
  labConfirmRearm_ = false;
  if (status_) status_->SetText(
      "LAB quota exhausted.\n"
      "Tap Rearm lab, then Confirm.\n"
      "No waiting period.\n"
      "No remote reset allowed.");
  if (labStart_) {
    labStart_->SetText("Rearm lab");
    lv_obj_remove_state(labStart_->LvglSelf(), LV_STATE_DISABLED);
  }
}

void PairingPreviewPage::showLabStorageError() {
  labStorageError_ = true;
  labLocked_ = false;
  labConfirmRearm_ = false;
  if (status_) status_->SetText(
      "LAB quota NVS invalid/error.\n"
      "No network listener started.\n"
      "Cannot safely auto-reset.\n"
      "Preserve NVS for diagnosis.");
  if (labStart_) {
    labStart_->SetText("NVS error");
    lv_obj_add_state(labStart_->LvglSelf(), LV_STATE_DISABLED);
  }
}

void PairingPreviewPage::rearmEncryptedLabFromTouch() {
  // Only called by the direct LVGL touchscreen button above. This operation
  // cannot be requested through the Python client, HTTP, MQTT, USB, a
  // reboot, timer, or any HACS API. It NEVER begins a pairing transaction.
  if (cancelled_ || !labLocked_ || labRearmed_ || labProbe_.active())
    return;
  const std::uint32_t now = millis();
  constexpr std::uint32_t kConfirmWindowMs = 15000;
  if (!labConfirmRearm_) {
    labConfirmRearm_ = true;
    labConfirmAtMs_ = now;
    if (status_) status_->SetText(
        "Confirm local LAB reset?\n"
        "Only windows + secmsgs.\n"
        "Touch Confirm within 15s.\n"
        "No HA/config change.");
    if (labStart_) labStart_->SetText("Confirm");
    return;
  }
  if (static_cast<std::uint32_t>(now - labConfirmAtMs_) > kConfirmWindowMs) {
    labConfirmRearm_ = false;
    if (status_) status_->SetText(
        "Confirmation expired.\n"
        "Tap Rearm lab again.\n"
        "No counters changed.");
    if (labStart_) labStart_->SetText("Rearm lab");
    return;
  }

  // NVS must remain valid; corruption is not equivalent to a PIN quota.
  constexpr std::uint32_t kMaximum = 
      config_v2_pairing_attempt_budget::kMaximumAttempts;
  const auto state = labAttemptStore_.lockState(kMaximum, kMaximum);
  if (state == config_v2_sec2_lab_budget::LockState::StorageError) {
    showLabStorageError();
    return;
  }
  if (state == config_v2_sec2_lab_budget::LockState::Exhausted &&
      !labAttemptStore_.reset()) {
    showLabStorageError();
    return;
  }
  labLocked_ = false;
  labRearmed_ = true;
  labConfirmRearm_ = false;
  if (status_) status_->SetText(
      "LAB quota rearmed locally.\n"
      "Close and reopen this page\n"
      "for a fresh six-digit code.\n"
      "No pairing or deployment.");
  if (labStart_) {
    labStart_->SetText("Rearmed");
    lv_obj_add_state(labStart_->LvglSelf(), LV_STATE_DISABLED);
  }
}

void PairingPreviewPage::startEncryptedLabFromTouch() {
  // A local Settings touchscreen tap is required for every lab window.
  // Reserve nonvolatile attempt budget *before* opening any network socket.
  // This counts lab window openings, not individual SRP attempts: never
  // deploy this as production pairing or claim brute-force protection.
  if (cancelled_ || labLocked_ || labRearmed_ || labStorageError_ ||
      labProbe_.active() ||
      pairing_.phase(millis()) != config_v2_pairing_policy::Phase::ShowCode)
    return;
  // Diagnose BOTH independent flash-persistent laboratory budgets before
  // consuming an additional window. A used-up Security2 frame budget must
  // not cause us to open another empty port-18082 listener.
  constexpr std::uint32_t kMaximum =
      config_v2_pairing_attempt_budget::kMaximumAttempts;
  const auto quota = labAttemptStore_.lockState(kMaximum, kMaximum);
  if (quota == config_v2_sec2_lab_budget::LockState::Exhausted) {
    showLabLockout();
    return;
  }
  if (quota == config_v2_sec2_lab_budget::LockState::StorageError) {
    showLabStorageError();
    return;
  }
  if (!WiFi.isConnected()) {
    if (status_) status_->SetText("Connect Wi-Fi before starting the lab.");
    return;
  }
  const std::string pin = pairing_.codeForLocalScreen(millis());
  if (pin.size() != 6) return;
  const auto reservation = labAttempts_.reserve(millis());
  if (reservation != config_v2_pairing_attempt_budget::Status::Allowed) {
    if (reservation == config_v2_pairing_attempt_budget::Status::Locked)
      showLabLockout();
    else
      showLabStorageError();
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
