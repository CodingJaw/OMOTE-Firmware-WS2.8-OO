"""Offline Security2 patch-1 hardening requirements and code boundary guards.

Does not connect to a Waveshare, consume PIN attempts or enable pairing.
"""
from __future__ import annotations

import json
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[2]
TEST=Path(__file__).resolve().parent
POLICY=json.loads((TEST/"sec2_nonce1_negative_acceptance_contract.json").read_text())
REVIEW=(ROOT/"lib/esp32HalImpl/wifiHandler/config_v2_sec2_lab_mutual_review.hpp").read_text()
COORD=(ROOT/"lib/WaveshareOmote/config_v2_sec2_tx_v2_lab.cpp").read_text()
TX_HEAD=(ROOT/"lib/WaveshareOmote/config_v2_sec2_tx_v2_lab.hpp").read_text()
PROBE=(ROOT/"lib/WaveshareOmote/config_v2_sec2_lab_transport.cpp").read_text()
CLIENT=(TEST/"sec2_lab_client.py").read_text()
HOST_RUN=(TEST/"run-host-tests.sh").read_text()


class Security2Nonce1NegativeAcceptanceTests(unittest.TestCase):
    def test_timeout_and_quota_boundaries_match_implementation(self):
        wire=POLICY["transport"]
        self.assertEqual(wire["required_security2_patch"],1)
        self.assertEqual(wire["transaction_timeout_ms"],30000)
        self.assertEqual(wire["touch_review_timeout_ms"],60000)
        self.assertEqual(wire["lab_window_timeout_ms"],120000)
        self.assertEqual(wire["expected_persistent_maximum_security_messages"],5)
        self.assertIn("kTransactionTimeoutMs = 30000",TX_HEAD)
        self.assertIn("kReviewMs = 60000",REVIEW)
        self.assertIn("kWindowMs = 120000",REVIEW)
        self.assertIn("kMaxWindowMs",PROBE)
        self.assertIn("kMaximumMessages = 5",PROBE)
        self.assertIn("securityPatchVersion(protocomm_security2, 0) != 1",PROBE)

    def test_all_negative_host_requirements_are_declared(self):
        expected={
            "reject_fixed_nonce_patch0_before_pin_or_srp_in_strict_mode",
            "reject_stale_ciphertext_replay_with_counter",
            "reject_invalid_aes_gcm_tag_and_fail_session_closed",
            "never_resume_missing_response_on_old_nonce_counter",
            "require_new_srp_key_and_nonce_prefix_on_reconnect",
            "reject_counter_overflow",
            "scrub_unacknowledged_fingerprints_at_exact_touch_deadline",
            "scrub_reviewed_fingerprints_at_exact_listener_deadline",
            "scrub_verified_evidence_on_physical_reject_or_listener_close",
            "one_device_signed_receipt_maximum_per_completed_transaction",
            "never_set_paired_or_trusted_or_deployment_enabled",
        }
        self.assertTrue(expected.issubset(POLICY["host_requirements"]))
        self.assertIn("sec2_lab_review_lifecycle",HOST_RUN)
        self.assertIn("--require-sec2-patch1",CLIENT)
        self.assertIn("touchReceiptIssued_=true;",COORD)
        self.assertIn("touchReceiptIssued_ || completedSession_!=session",COORD)
        self.assertIn("verifiedSession",REVIEW)
        self.assertIn("rejectFromLocalTouch()",REVIEW)

    def test_hardware_tests_remain_explicit_and_not_automated(self):
        needed=POLICY["hardware_required_before_production"]
        self.assertIn("verify_real_reconnect_uses_new_srp_key_and_nonce",needed)
        self.assertIn("verify_invalid_PIN_persistent_quota_without_implicit_reset",
                      needed)
        self.assertIn("review_authenticated_capability_negotiation_before_trust",
                      needed)
        isolation=POLICY["environment_isolation"]
        self.assertTrue(all(isolation.values()))
        self.assertNotIn("esptool",HOST_RUN)
        self.assertNotIn("prov-session",HOST_RUN)

    def test_no_new_authority(self):
        auth=POLICY["authorization"]
        self.assertTrue(auth["laboratory_only"])
        self.assertTrue(all(v is False for k,v in auth.items()
                            if k!="laboratory_only"))


if __name__=="__main__":
    unittest.main()
