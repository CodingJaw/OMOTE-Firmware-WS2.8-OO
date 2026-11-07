"""Firmware-owned trust journal draft contract tests; no firmware writes."""
from __future__ import annotations
import json
from pathlib import Path
import unittest

DOC = Path(__file__).with_name("trust_journal_contract_v1.json")


class TrustJournalContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract = json.loads(DOC.read_text())

    def test_exact_public_record_and_forbidden_secrets(self):
        r = self.contract["public_record"]
        self.assertEqual(r["format"], "omote-trust-journal-v1-draft")
        self.assertEqual(len(r["exact_keys"]), len(set(r["exact_keys"])))
        self.assertIn("epoch", r["exact_keys"])
        self.assertIn("device_key_protection", r["exact_keys"])
        self.assertFalse(r["contains_private_key"])
        for forbidden in ("private_key", "pin", "credential", "token", "secret"):
            self.assertTrue(all(forbidden not in field for field in r["exact_keys"]))

    def test_no_trust_or_live_interface(self):
        d = self.contract
        self.assertFalse(d["protocol"]["network_endpoint_enabled"])
        self.assertFalse(d["protocol"]["credentials_or_trust_writes_enabled"])
        self.assertTrue(d["authorization"]["laboratory_only"])
        self.assertTrue(all(value is False for name, value in d["authorization"].items()
                            if name != "laboratory_only"))
        self.assertTrue(d["protocol"]["hardware_key_protection_required_before_production"])

    def test_non_atomic_enrollment_reconciliation_and_replay(self):
        d = self.contract["protocol"]
        self.assertTrue(d["no_claim_of_atomic_cross_device_commit"])
        self.assertTrue(d["loss_of_commit_receipt_requires_reconciliation_not_auto_authorization"])
        self.assertTrue(d["reboot_requires_new_authorized_reconnect_proof"])
        self.assertTrue(d["reject_duplicate_transaction_ids"])
        self.assertTrue(d["reject_non_monotonic_epochs"])
        self.assertTrue(d["revoke_is_tombstone_not_delete"])
        self.assertEqual(d["draft_procedure"][-1],
                         "fresh_mutual_challenge_for_any_future_authenticated_operation")

    def test_threat_scenarios_listed(self):
        cases = set(self.contract["threat_tests"])
        for expected in ("fake_peer_receipt", "torn_write", "reboot_mid_commit",
                         "lost_receipt", "asymmetric_commit", "revocation_replay",
                         "software_lab_key_not_production"):
            self.assertIn(expected, cases)


if __name__ == "__main__":
    unittest.main()
