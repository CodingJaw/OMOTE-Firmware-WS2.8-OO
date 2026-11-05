"""Real Python P-256 signature tests for the lab candidate-key proof.

No Home Assistant installation or firmware device is required. The firmware
build separately compiles the matching C++ Mbed TLS verifier.
"""
from __future__ import annotations

import unittest
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from sec2_lab_ha_proof import (
    DOMAIN, REPLY, check_lab_reply, make_candidate_proof, parse_session_cookie,
)


class CandidateKeyProofTests(unittest.TestCase):
    def test_fresh_proof_has_valid_p256_signature_and_session_binding(self):
        cookie = "session=12345678"
        proof = make_candidate_proof(cookie)
        self.assertEqual(proof[0], 1)
        self.assertEqual(proof[1], 4)
        self.assertEqual(len(proof[1:66]), 65)
        self.assertEqual(len(proof[66:98]), 32)
        sig_len = int.from_bytes(proof[98:100], "big")
        self.assertGreaterEqual(sig_len, 8)
        self.assertLessEqual(sig_len, 80)
        self.assertEqual(len(proof), 100 + sig_len)
        key = ec.EllipticCurvePublicKey.from_encoded_point(
            ec.SECP256R1(), proof[1:66]
        )
        signed = DOMAIN + (12345678).to_bytes(4, "big") + proof[1:98]
        key.verify(proof[100:], signed, ec.ECDSA(hashes.SHA256()))

        for tampered in (
            DOMAIN + (12345679).to_bytes(4, "big") + proof[1:98],
            signed[:-1] + bytes([signed[-1] ^ 1]),
            b"OTHER-PROTOCOL" + signed[len(DOMAIN):],
        ):
            with self.subTest(tampered=tampered[:22]):
                with self.assertRaises(InvalidSignature):
                    key.verify(proof[100:], tampered, ec.ECDSA(hashes.SHA256()))

    def test_proofs_are_ephemeral_not_reused(self):
        a = make_candidate_proof("session=1")
        b = make_candidate_proof("session=1")
        self.assertNotEqual(a[1:66], b[1:66])
        self.assertNotEqual(a[66:98], b[66:98])
        self.assertNotEqual(a[100:], b[100:])

    def test_reject_malformed_or_out_of_range_cookies(self):
        invalid = ("", "sid=12", "session=-1", "session=+1", "session=01",
                   "session=4294967296", "session=1; Path=/",
                   "session=1\nX: admin", "session=1;session=2",
                   "session=1 ", "SESSION=1", None, 1)
        for item in invalid:
            with self.subTest(item=item), self.assertRaises(ValueError):
                parse_session_cookie(item)
        self.assertEqual(parse_session_cookie("session=0"), 0)
        self.assertEqual(parse_session_cookie("session=4294967295"), 0xFFFFFFFF)

    def test_reply_is_only_unprivileged_lab_receipt(self):
        check_lab_reply(REPLY)
        for invalid in (b"", "OK", b"OK", b"OMOTE-PAIRED", REPLY + b"x"):
            with self.subTest(reply=invalid), self.assertRaises(ValueError):
                check_lab_reply(invalid)


if __name__ == "__main__":
    unittest.main()
