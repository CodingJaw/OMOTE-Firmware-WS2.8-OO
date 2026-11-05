"""Offline, cryptographically real P-256 device proof wire-format tests.

These prove the Python verifier is strict; the matching firmware C++ build
and physical Waveshare run are separate independent acceptance tests.
"""
from __future__ import annotations

from hashlib import sha256
import unittest

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

from sec2_lab_device_proof import (
    DOMAIN, make_challenge, parse_session_cookie, verify_device_proof,
)


def make_response(key, session: int, challenge: bytes) -> bytes:
    public = key.public_key().public_bytes(
        serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint,
    )
    signature = key.sign(
        DOMAIN + session.to_bytes(4, "big") + challenge[1:] + public,
        ec.ECDSA(hashes.SHA256()),
    )
    return b"\x01" + public + len(signature).to_bytes(2, "big") + signature


class DeviceIdentityWireTests(unittest.TestCase):
    def setUp(self):
        self.key = ec.generate_private_key(ec.SECP256R1())
        self.challenge = make_challenge()
        self.session = 987654321
        self.cookie = f"session={self.session}"
        self.response = make_response(self.key, self.session, self.challenge)

    def test_real_signature_verifies_exact_session_nonce_and_key(self):
        fingerprint = verify_device_proof(
            self.cookie, self.challenge, self.response
        )
        self.assertEqual(fingerprint, sha256(self.response[1:66]).hexdigest())
        self.assertEqual(len(fingerprint), 64)

    def test_bad_session_nonce_domain_or_signature_fails(self):
        with self.assertRaises(InvalidSignature):
            verify_device_proof("session=987654322", self.challenge, self.response)
        with self.assertRaises(InvalidSignature):
            modified = self.challenge[:-1] + bytes([self.challenge[-1] ^ 1])
            verify_device_proof(self.cookie, modified, self.response)
        with self.assertRaises(InvalidSignature):
            altered = bytearray(self.response)
            altered[-1] ^= 1
            verify_device_proof(self.cookie, self.challenge, bytes(altered))
        wrong = ec.generate_private_key(ec.SECP256R1())
        with self.assertRaises(InvalidSignature):
            verify_device_proof(
                self.cookie, self.challenge,
                b"\x01" + wrong.public_key().public_bytes(
                    serialization.Encoding.X962,
                    serialization.PublicFormat.UncompressedPoint
                ) + self.response[66:]
            )

    def test_bad_frames_and_session_cookie_fail_before_trust(self):
        for reply in (
            b"", b"\x01", b"\x02" + self.response[1:],
            self.response[:-1], self.response + b"x",
            self.response[:66] + b"\x00\x00" + self.response[68:],
            self.response[:66] + b"\xff\xff" + self.response[68:],
            b"\x01\x05" + self.response[2:],
        ):
            with self.subTest(reply=reply[:4]), self.assertRaises(ValueError):
                verify_device_proof(self.cookie, self.challenge, reply)

        for cookie in ("", "session=-1", "session=01",
                       "session=4294967296", "session=1; Path=/", None):
            with self.subTest(cookie=cookie), self.assertRaises(ValueError):
                parse_session_cookie(cookie)

    def test_random_nonce_and_no_persisted_private_key(self):
        self.assertEqual(len(self.challenge), 33)
        self.assertEqual(self.challenge[0], 1)
        self.assertNotEqual(self.challenge, make_challenge())


if __name__ == "__main__":
    unittest.main()
