"""Regression tests: Security2 Python API mismatch must be caught before SRP traffic.

Purely local; none of these tests opens a socket, prompts for a PIN, or
consumes the Waveshare's persistent Security2 attempt budget.
"""
import unittest

from sec2_lab_client import LabProbeError, _construct_security2


class LegacySecurity2:
    """Matches Espressif ESP-IDF v5.3.2 constructor."""
    def __init__(self, username, password, verbose):
        self.username = username
        self.password = password
        self.verbose = verbose


class PatchedSecurity2:
    """Models versions with an explicit sec_patch_ver parameter."""
    def __init__(self, username, password, verbose, sec_patch_ver):
        self.username = username
        self.password = password
        self.verbose = verbose
        self.sec_patch_ver = sec_patch_ver


class UnknownSignature:
    __signature__ = "invalid signature"


class Security2ConstructorCompatibilityTests(unittest.TestCase):
    def test_legacy_patch_zero_constructs_without_unknown_keyword(self):
        obj = _construct_security2(LegacySecurity2, 0, "012345")
        self.assertIsInstance(obj, LegacySecurity2)
        self.assertEqual(obj.username, "omote-ha")
        self.assertEqual(obj.password, "012345")
        self.assertFalse(obj.verbose)

    def test_legacy_patch_one_rejected_before_handshake(self):
        with self.assertRaises(LabProbeError) as error:
            _construct_security2(LegacySecurity2, 1, "012345")
        self.assertEqual(error.exception.stage, "srp-api")
        self.assertEqual(error.exception.kind, "UnsupportedNoncePatch")

    def test_newer_patch_zero_constructs(self):
        obj = _construct_security2(PatchedSecurity2, 0, "012345")
        self.assertEqual(obj.sec_patch_ver, 0)

    def test_newer_patch_one_is_forwarded(self):
        obj = _construct_security2(PatchedSecurity2, 1, "012345")
        self.assertEqual(obj.sec_patch_ver, 1)

    def test_unknown_signature_fails_closed(self):
        with self.assertRaises(LabProbeError) as error:
            _construct_security2(UnknownSignature, 0, "012345")
        self.assertEqual(error.exception.stage, "srp-api")


if __name__ == "__main__":
    unittest.main()
