"""Firmware-owned offline lifecycle contract review.

This test never opens a network port, executes Security2, writes NVS or
introduces a pairing state machine to the running firmware. It pins future
firmware implementation requirements with production trust still prohibited.
"""
from __future__ import annotations

import json
from pathlib import Path
import unittest

CONTRACT = Path(__file__).with_name("pairing_lifecycle_contract_v1.json")


class PairingLifecycleContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = json.loads(CONTRACT.read_text())

    def test_unique_states_events_and_valid_transitions(self):
        d = self.data
        states, events = d["states"], d["events"]
        self.assertEqual(len(states), len(set(states)))
        self.assertEqual(len(events), len(set(events)))
        self.assertIn("paired", states)
        self.assertIn("revoked", states)
        seen = set()
        for transition in d["allowed_transitions"]:
            self.assertIn(transition["from"], states)
            self.assertIn(transition["to"], states)
            self.assertIn(transition["event"], events)
            key = (transition["from"], transition["event"])
            self.assertNotIn(key, seen)
            seen.add(key)

    def test_pairing_never_skips_proofs_or_local_review(self):
        transitions = {
            (t["from"], t["event"]): t["to"]
            for t in self.data["allowed_transitions"]
        }
        self.assertEqual(transitions[("window_open", "verify_both")],
                         "proofs_verified")
        self.assertEqual(transitions[("proofs_verified", "present_review")],
                         "review_pending")
        self.assertEqual(transitions[("review_pending", "approve")],
                         "review_approved")
        self.assertEqual(transitions[("review_pending", "reject")],
                         "unpaired")
        self.assertEqual(transitions[("review_approved", "commit_trust")],
                         "paired")
        self.assertNotIn(("window_open", "approve"), transitions)
        self.assertNotIn(("proofs_verified", "commit_trust"), transitions)
        self.assertNotIn(("review_pending", "commit_trust"), transitions)

    def test_production_security_gates_are_necessary_but_disabled(self):
        d = self.data
        self.assertEqual(d["scope"],
                         "offline_policy_only_no_trust_persistence_or_network_registration")
        self.assertFalse(any(d["authorization"].values()))
        self.assertTrue(d["rules"]["require_device_identity_hardware_protection_before_trust"])
        self.assertTrue(d["rules"]["require_mutual_durable_commit_before_paired"])
        self.assertTrue(d["rules"]["require_ha_admin_approval"])
        self.assertTrue(d["rules"]["require_exact_fingerprint_and_touchscreen_approval"])
        self.assertTrue(d["rules"]["reboot_clears_uncommitted_approval"])
        self.assertEqual(d["rules"]["window_seconds"], 120)
        self.assertEqual(d["rules"]["review_seconds"], 60)

    def test_production_reconnect_and_revocation_require_separate_design(self):
        d = self.data
        self.assertTrue(d["rules"]["reconnect_requires_fresh_mutual_challenge_without_pin"])
        self.assertTrue(d["rules"]["revoke_requires_fresh_physical_approval"])
        self.assertFalse(d["authorization"]["authenticated_reconnect_enabled"])
        self.assertFalse(d["authorization"]["durable_trust_write_enabled"])
        self.assertFalse(d["authorization"]["efuse_changes_enabled"])


if __name__ == "__main__":
    unittest.main()
