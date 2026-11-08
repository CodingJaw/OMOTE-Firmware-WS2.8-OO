"""Keep the experimental encrypted probe distinct from production pairing.

These static tests complement the dedicated Waveshare and lab build targets.
They are NOT proofs of actual cryptographic interoperability on hardware.
"""
from __future__ import annotations

from pathlib import Path
import json
import unittest

ROOT = Path(__file__).resolve().parents[2]
LIB = ROOT / "lib/WaveshareOmote"
SRC = (LIB / "config_v2_sec2_lab_transport.cpp").read_text()
HEADER = (LIB / "config_v2_sec2_lab_transport.hpp").read_text()
CONFIG = (ROOT / "platformio.ini").read_text()
HTTP = (ROOT / "lib/esp32HalImpl/wifiHandler/config_http.cpp").read_text()


class Security2LabBoundaryTests(unittest.TestCase):
    def test_lab_flag_not_in_default_waveshare(self):
        normal = CONFIG.split("[env:waveshare28v2_omote]", 1)[1].split(
            "[env:waveshare28v2_omote_sec2_lab]", 1
        )[0]
        self.assertNotIn("-D OMOTE_CONFIG_V2_SEC2_LAB_PROBE", normal)
        lab = CONFIG.split("[env:waveshare28v2_omote_sec2_lab]", 1)[1]
        self.assertIn("extends = env:waveshare28v2_omote", lab)
        self.assertIn("-D OMOTE_CONFIG_V2_SEC2_LAB_PROBE=1", lab)
        self.assertIn("#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)", SRC)
        self.assertIn("return false;", SRC.split("#else  // Lab transport", 1)[1])

    def test_probe_is_not_auto_started_or_network_triggered(self):
        self.assertIn("bool Probe::startFromTrustedLocalTouch(", SRC)
        self.assertIn("kMaxWindowMs = 120000", HEADER)
        self.assertIn("protocomm_httpd_start(", SRC)
        self.assertIn("protocomm_httpd_stop(", SRC)
        self.assertIn("config.data.config.port = kPort;", SRC)
        self.assertIn("kPort = 18082", HEADER)
        self.assertNotIn("18082", HTTP)
        self.assertNotIn("startFromTrustedLocalTouch", HTTP)
        # Zero actual entrypoints until a physically confirmed on-device UI
        # is tested; this prevents a network caller from activating the probe.
        for p in (ROOT / "src").rglob("*"):
            if p.suffix in (".cpp", ".hpp", ".h"):
                self.assertNotIn("startFromTrustedLocalTouch", p.read_text(), p.name)

    def test_lab_has_three_base_endpoints_plus_two_flagged_v2_endpoints(self):
        self.assertIn('kSessionEndpoint[] = "prov-session"', HEADER)
        self.assertIn('kProbeEndpoint[] = "omote-probe"', HEADER)
        self.assertEqual(SRC.count("protocomm_add_endpoint("), 5)
        self.assertIn("#if defined(OMOTE_CONFIG_V2_SEC2_TX_V2_LAB)", SRC)
        self.assertIn("kTxV2OpenEndpoint,txV2OpenRequest,this", SRC)
        self.assertIn("kTxV2CommitEndpoint,txV2CommitRequest,this", SRC)
        self.assertIn("config_v2_sec2_tx_v2_lab::Coordinator txV2_", HEADER)
        self.assertIn('kCandidateProofEndpoint[] = "omote-ha-proof"', HEADER)
        self.assertIn('kDeviceProofEndpoint[] = "omote-device-proof"', HEADER)
        self.assertIn('candidate, kDeviceProofEndpoint, deviceProofRequest, this', SRC)
        self.assertIn("deviceIdentity_.signSessionChallenge(", SRC)
        self.assertIn("config_v2_sec2_lab_mutual_review::Review labReview_", HEADER)
        self.assertIn("std::mutex reviewMutex_", HEADER)
        self.assertIn("labReview_.beginFromLocalTouch(nowMs)", SRC)
        self.assertIn("labReview_.observeVerifiedHa(session, fp, millis())", SRC)
        self.assertIn("labReview_.observeVerifiedDevice(session, fp, millis())", SRC)
        self.assertIn("std::lock_guard<std::mutex> lock(reviewMutex_)", SRC)
        self.assertIn("std::lock_guard<std::mutex> lock(self->reviewMutex_)", SRC)
        self.assertIn("labReview_.close()", SRC)
        self.assertNotIn("trust_.replaceAfterAuthenticatedPhysicalApproval", SRC)
        self.assertIn('candidate, kCandidateProofEndpoint, candidateProofRequest, this', SRC)
        self.assertIn('verifyCandidate(', SRC)
        self.assertIn("protocomm_set_security(", SRC)
        self.assertIn("boundedSecurity_ = protocomm_security2;", SRC)
        self.assertIn("boundedSecurity_.security_req_handler = &Probe::gatedSecurityRequest;", SRC)
        self.assertIn("&boundedSecurity_, &securityParams_", SRC)
        self.assertIn("securityParams_ = material_.params()", SRC)
        self.assertIn("protocomm_security2_params_t securityParams_", HEADER)
        self.assertIn('static constexpr char kReply[] = "OMOTE-SEC2-OK-V1"', SRC)
        for dangerous in (
            "handleFsWrite", "handleFsDelete", "/api/device/settings",
            "config_v2_single_active_commit", "replaceAfterAuthenticatedPhysicalApproval",
            "handleV1MutationDisabled", "passphrase", "mqtt", "ftpRestoreCredentials"
        ):
            self.assertNotIn(dangerous, SRC)
        self.assertIn("material_.clear()", SRC)
        self.assertIn("deviceIdentity_.prepareFromTrustedLocalTouch()", SRC)
        self.assertIn("deviceIdentity_.clear()", SRC)
        identity = (LIB / "config_v2_sec2_lab_device_identity.cpp").read_text()
        self.assertIn('kNamespace[] = "omsec2id"', identity)
        self.assertIn('kRecordKey[] = "labkey"', identity)
        self.assertIn("storage.putBytes(kRecordKey,", identity)
        self.assertIn("storage.getBytesLength(kRecordKey)", identity)
        self.assertIn("validate(verifier, check)", identity)
        self.assertIn("mbedtls_ecp_check_privkey(", identity)
        self.assertIn("mbedtls_ecp_mul(", identity)
        # In the bundled ESP-IDF 5.3.2 Mbed TLS headers, ecp_group.G is
        # public; MBEDTLS_PRIVATE(G) expands to nonexistent private_G.
        self.assertIn("&crypto.key.MBEDTLS_PRIVATE(grp).G,", identity)
        self.assertNotIn(".MBEDTLS_PRIVATE(G)", identity)
        self.assertIn("mbedtls_ecdsa_write_signature(", identity)
        self.assertNotIn("esp_efuse_write", identity)
        self.assertNotIn("replaceAfterAuthenticatedPhysicalApproval", identity)
        verifier = (LIB / "config_v2_sec2_lab_peer_proof.cpp").read_text()
        contract = (LIB / "config_v2_sec2_lab_peer_proof.hpp").read_text()
        self.assertIn("mbedtls_ecdsa_read_signature(", verifier)
        self.assertIn("mbedtls_ecp_check_pubkey(", verifier)
        self.assertIn("authenticatedSec2Session >> 24", verifier)
        self.assertIn("OMOTE-SEC2-LAB-HA-PROOF-V1", contract)
        self.assertIn("OMOTE-LAB-HA-PROOF-OK-V1", contract)
        self.assertNotIn("Preferences", verifier)
        self.assertNotIn("replaceAfterAuthenticatedPhysicalApproval", verifier)
        # Reject an actual call but permit a comment documenting its absence.
        self.assertNotRegex(SRC, r"(?m)^\s*(?:return\s+)?protocomm_get_sec_version\(")
        self.assertIn("securityPatchVersion(boundedSecurity_, 0)", SRC)
        self.assertIn("return 0;", SRC)
        self.assertIn("versionNumber == 2", SRC)
        self.assertIn("patchNumber == 0 || patchNumber == 1", SRC)

    def test_only_physically_opened_settings_preview_can_start_lab(self):
        page = (ROOT / "lib/BasicUI/page/PairingPreviewPage.cpp").read_text()
        header = (ROOT / "lib/BasicUI/page/PairingPreviewPage.hpp").read_text()
        settings = (ROOT / "lib/BasicUI/page/SettingsPage.cpp").read_text()
        self.assertIn("#if defined(OMOTE_CONFIG_V2_SEC2_LAB_PROBE)", page)
        self.assertIn("AddNewElement<UI::Widget::Button>", page)
        self.assertIn("if (labLocked_) rearmEncryptedLabFromTouch();", page)
        self.assertIn("else startEncryptedLabFromTouch();", page)
        self.assertIn("PairingPreviewPage::startEncryptedLabFromTouch()", page)
        self.assertIn("labAttempts_.reserve(millis())", page)
        self.assertLess(
            page.index("labAttempts_.reserve(millis())"),
            page.index("labProbe_.startFromTrustedLocalTouch("),
        )
        self.assertIn("labProbe_.tick(now)", page)
        self.assertIn("labProbe_.stop()", page)
        self.assertIn("labProbe_.labReviewSnapshot(now)", page)
        self.assertIn("labProbe_.acknowledgeLabReviewFromLocalTouch(millis())", page)
        self.assertIn("labProbe_.rejectLabReviewFromLocalTouch()", page)
        self.assertIn('approve_->SetText("Review OK")', page)
        self.assertIn("NO HA trust was stored.", page)
        self.assertIn("NO TRUST, NO DEPLOYMENT.", page)
        self.assertIn("fp.substr(48, 16)", page)
        self.assertIn("State::AwaitingTouch", page)
        # Hardware report: the Start lab widget was constructed after
        # Review OK at the same bottom-left position, occluding the review
        # button. The mutually verified proof state must hide Start lab
        # before un-hiding Review OK, without reopening the listener.
        review_start = page.index(
            "if (review.state ==\n"
            "        config_v2_sec2_lab_mutual_review::State::AwaitingTouch)"
        )
        review_end = page.index(
            "if (review.state == config_v2_sec2_lab_mutual_review::State::Reviewed)",
            review_start,
        )
        awaiting = page[review_start:review_end]
        hide_overlapping = (
            'lv_obj_add_flag(labStart_->LvglSelf(), LV_OBJ_FLAG_HIDDEN)'
        )
        show_review = (
            'lv_obj_remove_flag(approve_->LvglSelf(), LV_OBJ_FLAG_HIDDEN)'
        )
        self.assertIn(hide_overlapping, awaiting)
        self.assertIn(show_review, awaiting)
        self.assertLess(awaiting.index(hide_overlapping), awaiting.index(show_review))
        self.assertNotIn("labProbe_.startFromTrustedLocalTouch(", awaiting)
        self.assertNotIn("labAttempts_.reserve(", awaiting)
        self.assertNotIn("labAttemptStore_.reset()", awaiting)
        self.assertIn('approve_->SetText("Review OK")', page)
        self.assertEqual(page.count('approve_->SetText("Review OK")'), 1)
        self.assertIn('reject_->SetText("Reject")', awaiting)
        # A started lab listener keeps a visible, disabled on-screen marker.
        # This must never become a second start action or reopen an NVS window.
        self.assertIn('labStart_->SetText("Start lab")', page)
        self.assertIn('labStart_->SetText("Lab active")', page)
        self.assertIn('labStart_->SetText("Lab expired")', page)
        self.assertIn('labStart_->SetText("Rearm lab")', page)
        self.assertIn('labStart_->SetText("Confirm")', page)
        self.assertIn('labStart_->SetText("Rearmed")', page)
        self.assertIn('labStart_->SetText("NVS error")', page)
        self.assertIn('lv_obj_add_state(labStart_->LvglSelf(), LV_STATE_DISABLED)', page)
        success = page.split('if (status_) status_->SetText(\n      "Encrypted LAB test port 18082.', 1)[1]
        self.assertNotIn('lv_obj_add_flag(labStart_->LvglSelf(), LV_OBJ_FLAG_HIDDEN)', success)
        self.assertIn("config_v2_sec2_lab_budget::Store", header)
        # Only a physically initiated two-touch confirmation can reset quotas.
        # LAB lockout intentionally has NO wait; generic production pairing
        # policy may retain an unrelated cooldown.
        self.assertIn("PairingPreviewPage::rearmEncryptedLabFromTouch()", page)
        self.assertNotIn("config_v2_pairing_attempt_budget::kRearmDelayMs", page)
        self.assertNotIn("labLockedAtMs_", page + header)
        self.assertNotIn("Rearm cooling down", page)
        self.assertIn("No waiting period.", page)
        self.assertIn("kConfirmWindowMs = 15000", page)
        self.assertIn("now - labConfirmAtMs_", page)
        self.assertIn("labConfirmRearm_ = true;", page)
        self.assertIn("labAttemptStore_.reset()", page)
        self.assertIn("if (!labConfirmRearm_)", page)
        self.assertEqual(page.count("labAttemptStore_.reset()"), 1)
        self.assertLess(
            page.index("labConfirmRearm_ = true;"),
            page.index("labAttemptStore_.reset()")
        )
        self.assertIn("if (labLocked_ || labRearmed_ || labStorageError_) return;", page)
        self.assertIn("labAttemptStore_.lockState(kMaximum, kMaximum)", page)
        self.assertLess(
            page.index("labAttemptStore_.lockState(kMaximum, kMaximum)", page.index("startEncryptedLabFromTouch() {")),
            page.index("labAttempts_.reserve(millis())")
        )
        budget = (ROOT / "lib/WaveshareOmote/config_v2_sec2_lab_budget.cpp").read_text()
        self.assertIn('kNamespace[] = "omsec2lab"', budget)
        self.assertNotIn('kNamespace[] = "omotepair"', budget)
        self.assertIn('nvs.getUInt(key, UINT32_MAX) == next', budget)
        self.assertIn("LockState Store::lockState(", budget)
        self.assertIn("bool Store::reset()", budget)
        self.assertIn("nvs.putUInt(kCounter, 0u)", budget)
        self.assertIn("nvs.putUInt(kSecMessageCounter, 0u)", budget)
        self.assertIn("windowsVerified && messagesVerified", budget)
        self.assertIn("config_v2_pairing_policy::Entry::SettingsTouch", settings)
        # The listener is never started from an HTTP/MQTT command or boot
        # hardware initialization. Default build omits its implementation.
        for p in (
            ROOT / "lib/WaveshareOmote/WaveshareHardware.cpp",
            ROOT / "lib/esp32HalImpl/wifiHandler/config_http.cpp",
            ROOT / "lib/BasicUI/BasicUI.cpp",
        ):
            self.assertNotIn("startEncryptedLabFromTouch", p.read_text(), p.name)
            self.assertNotIn("startFromTrustedLocalTouch(", p.read_text(), p.name)

    def test_every_srp_frame_pays_persistent_quota_before_crypto(self):
        header = (LIB / "config_v2_sec2_lab_transport.hpp").read_text()
        budget = (LIB / "config_v2_sec2_lab_budget.cpp").read_text()
        self.assertIn("gatedSecurityRequest(", SRC)
        self.assertIn("gatedSecurityRequest(", header)
        self.assertIn("&Probe::gatedSecurityRequest", SRC)
        self.assertIn("boundedSecurity_.security_req_handler", SRC)
        self.assertIn("&boundedSecurity_, &securityParams_", SRC)
        self.assertIn("securityParams_ = material_.params()", SRC)
        self.assertIn("protocomm_security2_params_t securityParams_", HEADER)
        before = SRC.index("lab->messageStore_.reserveSecurityMessage(")
        after = SRC.index("return protocomm_security2.security_req_handler(")
        self.assertLess(before, after)
        self.assertIn("kMaximumMessages = 5", SRC)
        self.assertIn('kSecMessageCounter[] = "secmsgs"', budget)
        self.assertIn('kNamespace[] = "omsec2lab"', budget)
        self.assertIn("nvs.putUInt(key, next)", budget)
        self.assertIn("nvs.getUInt(key, UINT32_MAX) == next", budget)
        self.assertNotIn("resetSecurityMessage", header)
        self.assertIn("activeLab_ == this", SRC)
        self.assertIn("esp_restart()", SRC)

    def test_shared_candidate_proof_wire_contract(self):
        spec = json.loads(
            (Path(__file__).with_name("sec2_lab_ha_proof_contract.json")).read_text()
        )
        req = spec["request"]
        self.assertEqual(spec["transport"]["endpoint"], "omote-ha-proof")
        self.assertEqual(spec["transport"]["port"], 18082)
        self.assertTrue(spec["transport"]["must_be_inside_completed_encrypted_security2_session"])
        self.assertEqual(req["version"], 1)
        self.assertEqual(req["header_bytes"], 100)
        self.assertEqual(req["signature_max_bytes"], 80)
        self.assertIn(req["domain"], (LIB / "config_v2_sec2_lab_peer_proof.hpp").read_text())
        self.assertIn(spec["response"]["authenticated_encrypted_payload_ascii"],
                      (LIB / "config_v2_sec2_lab_peer_proof.hpp").read_text())
        self.assertTrue(all(v is False for v in spec["authorization"].values()))

    def test_manual_client_does_not_take_pin_as_cli_argument(self):
        client = (Path(__file__).with_name("sec2_lab_client.py")).read_text()
        self.assertIn("getpass(", client)
        self.assertIn("Security2(", client)
        self.assertIn("sec.encrypt_data(", client)
        self.assertIn("sec.decrypt_data(", client)
        self.assertNotIn('ap.add_argument("--pin"', client)
        self.assertNotIn("verbose=True", client)
        self.assertIn("--ack-lab-only", client)
        self.assertIn("--ha-proof", client)
        self.assertIn('details["sec_patch_ver"] not in (0, 1)', client)
        self.assertIn('_construct_security2(Security2, details["sec_patch_ver"], pin)', client)
        self.assertIn('stage = "srp-api"', client)
        self.assertIn('make_candidate_proof(cookie)', client)
        self.assertIn("sha256(proof[1:66]).hexdigest()", client)
        self.assertIn("Compare the candidate HA fingerprint with the Waveshare screen.", client)
        self.assertIn('sec.encrypt_data(proof)', client)
        self.assertIn('check_lab_reply(plaintext)', client)
        self.assertIn("--device-proof", client)
        self.assertNotIn('add_argument("--reset', client)
        self.assertNotIn('add_argument("--rearm', client)
        self.assertIn('verify_device_proof(cookie, challenge, plaintext)', client)
        self.assertIn('"omote-device-proof"', client)


if __name__ == "__main__":
    unittest.main()
