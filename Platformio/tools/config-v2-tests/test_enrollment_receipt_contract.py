"""Firmware-owned signed receipt draft contract verification, no device I/O."""
from __future__ import annotations
import json
from pathlib import Path
import unittest

DOC = Path(__file__).with_name("enrollment_receipt_contract_v1.json")


class EnrollmentReceiptContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.doc = json.loads(DOC.read_text())

    def test_canonical_fields_and_domain_separation(self):
        c = self.doc
        self.assertEqual(c["contract"], "omote-enrollment-receipt-v1-draft")
        self.assertEqual(c["domain"], "OMOTE-ENROLLMENT-RECEIPT-V1" + chr(10))
        self.assertEqual(len(c["body_fields"]), len(set(c["body_fields"])))
        for name in ("tx_id", "epoch", "nonce", "signer", "phase",
                     "device_id", "ha_key_sha256", "device_key_sha256",
                     "session_transcript_sha256"):
            self.assertIn(name, c["body_fields"])
        self.assertEqual(set(c["envelope_fields"]), {"body", "signature_der_hex"})

    def test_pinned_signer_phase_and_protocol(self):
        c = self.doc
        self.assertEqual(set(c["signers"]), {"ha", "device"})
        self.assertEqual(c["phases"], ["prepare", "commit_intent", "peer_confirmed"])
        self.assertEqual(c["signature"],
                         "ecdsa_p256_sha256_der_over_domain_prefixed_canonical_ascii_json")
        self.assertTrue(c["reconciliation"].endswith(";never_trust"))

    def test_no_live_permissions_or_endpoints(self):
        c = self.doc
        self.assertEqual(c["scope"], "offline_signed_receipt_verification_only")
        self.assertTrue(c["authorization"]["laboratory_only"])
        self.assertFalse(any(value for name, value in c["authorization"].items()
                             if name != "laboratory_only"))
        for requirement in ("forged_signatures_fail", "wrong_public_key_fails",
                            "wrong_session_binding_fails", "duplicate_nonce_fails",
                            "missing_second_peer_receipt_pending",
                            "software_lab_key_not_hardware_protected"):
            self.assertIn(requirement, c["acceptance"])


if __name__ == "__main__":
    unittest.main()
