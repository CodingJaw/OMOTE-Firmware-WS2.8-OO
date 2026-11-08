"""No-hardware tests for opt-in encrypted Security2 v2 transaction client."""
from __future__ import annotations
import importlib.util
from hashlib import sha256
from pathlib import Path
import sys
import unittest

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

ROOT=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location(
    "omote_sec2_live_tx2_client_test",ROOT/"sec2_tx_v2_lab_client.py")
# The companion module is an existing tool in the same directory.
sys.path.insert(0,str(ROOT))
mod=importlib.util.module_from_spec(spec)
sys.modules[spec.name]=mod
spec.loader.exec_module(mod)
from sec2_mutual_transaction_proof_v2 import (
    DEVICE_DOMAIN, canonical_context, derive_bound,
)


class TxV2LiveClientFramingTests(unittest.TestCase):
    def setUp(self):
        self.tx=mod.LabV2Transaction("session=123456")
        self.device=ec.generate_private_key(ec.SECP256R1())
        self.device_pub=self.device.public_key().public_bytes(
            serialization.Encoding.X962,serialization.PublicFormat.UncompressedPoint)
        self.open_req=self.tx.open_request()
        self.reply=(b"\x02"+(123456).to_bytes(4,"big")+(1).to_bytes(4,"big")+
                    bytes(range(16))+b"\x08"+
                    sha256(self.device_pub).digest()[:8]+self.device_pub+
                    bytes(range(32)))

    def commit_reply(self, *, digest_override=None):
        request=self.tx.commit_request(self.reply)
        self.assertEqual(request[:1],b"\x02")
        self.assertEqual(len(request),2+request[1])
        canonical=canonical_context(self.tx.context)
        signature=self.device.sign(DEVICE_DOMAIN+canonical,
                                  ec.ECDSA(hashes.SHA256()))
        envelope={"context":self.tx.context,
                  "ha_signature_der_hex":request[2:].hex(),
                  "device_signature_der_hex":signature.hex()}
        proof=derive_bound(envelope,observed=self.tx.context)
        digest=bytes.fromhex(proof["session_transcript_sha256"])
        if digest_override is not None:
            digest=digest_override
        return b"\x02"+bytes([len(signature)])+signature+digest

    def test_exact_v2_encrypted_request_and_success_without_pairing(self):
        self.assertEqual(len(self.open_req),98)
        self.assertEqual(self.open_req[0],2)
        result=self.tx.check_commit(self.commit_reply())
        self.assertFalse(result["paired"])
        self.assertFalse(result["trusted"])
        self.assertFalse(result["deployment_enabled"])
        self.assertEqual(len(result["session_transcript_sha256"]),64)

    def test_verified_public_fingerprints_match_signed_context(self):
        with self.assertRaises(mod.LabTransactionError):
            self.tx.verified_public_fingerprints()
        verified_reply = self.commit_reply()
        with self.assertRaises(mod.LabTransactionError):
            self.tx.verified_public_fingerprints()
        self.tx.check_commit(verified_reply)
        fingerprints = self.tx.verified_public_fingerprints()
        self.assertEqual(fingerprints["ha"], sha256(self.open_req[1:66]).hexdigest())
        self.assertEqual(fingerprints["device"], sha256(self.device_pub).hexdigest())

    def test_fingerprints_are_not_exposed_after_failed_signature(self):
        corrupted=bytearray(self.commit_reply())
        corrupted[2]^=1
        with self.assertRaises(mod.LabTransactionError):
            self.tx.check_commit(bytes(corrupted))
        with self.assertRaises(mod.LabTransactionError):
            self.tx.verified_public_fingerprints()

    def test_review_poll_only_after_live_mutual_proof(self):
        with self.assertRaises(mod.LabTransactionError):
            self.tx.review_request()
        with self.assertRaises(mod.LabTransactionError):
            self.tx.check_review_reply(bytes([2, 1]) + bytes(32))
        reply = self.commit_reply()
        with self.assertRaises(mod.LabTransactionError):
            self.tx.review_request()
        proof = self.tx.check_commit(reply)
        digest = bytes.fromhex(proof["session_transcript_sha256"])
        self.assertEqual(self.tx.review_request(), bytes([2]) + digest)
        self.assertFalse(self.tx.check_review_reply(bytes([2, 0]) + digest))
        self.assertTrue(self.tx.check_review_reply(bytes([2, 1]) + digest))

    def test_review_wrong_digest_version_or_status_rejected(self):
        proof = self.tx.check_commit(self.commit_reply())
        digest = bytes.fromhex(proof["session_transcript_sha256"])
        for invalid in (
            b"", None, bytes([1, 1]) + digest,
            bytes([2, 2]) + digest,
            bytes([2, 1]) + bytes(32),
            bytes([2, 1]) + digest[:-1],
            bytes([2, 1]) + digest + b"x",
        ):
            with self.subTest(invalid=repr(invalid)[:40]):
                with self.assertRaises(mod.LabTransactionError):
                    self.tx.check_review_reply(invalid)

    def _ready_touch_receipt(self):
        self.tx.check_commit(self.commit_reply())
        digest = self.tx.review_request()[1:]
        self.assertFalse(self.tx.check_review_reply(bytes([2, 0]) + digest))
        self.assertTrue(self.tx.check_review_reply(bytes([2, 1]) + digest))
        return self.tx.touch_receipt_request()

    def _sign_touch_receipt(self, request, *, key=None, domain=None,
                            session=123456, status=1):
        key = key or self.device
        domain = mod.TOUCH_RECEIPT_DOMAIN if domain is None else domain
        message = (domain + bytes([2]) + session.to_bytes(4, "big") +
                   request[1:33] + request[33:65] + bytes([status]))
        signature = key.sign(message, ec.ECDSA(hashes.SHA256()))
        return bytes([2, 1, len(signature)]) + signature

    def test_touch_receipt_requires_verified_proof_and_real_review(self):
        with self.assertRaises(mod.LabTransactionError):
            self.tx.touch_receipt_request()
        self.tx.check_commit(self.commit_reply())
        with self.assertRaises(mod.LabTransactionError):
            self.tx.touch_receipt_request()
        digest = self.tx.review_request()[1:]
        self.assertFalse(self.tx.check_review_reply(bytes([2, 0]) + digest))
        with self.assertRaises(mod.LabTransactionError):
            self.tx.touch_receipt_request()

    def test_device_signed_touch_receipt_validates_then_refuses_replay(self):
        req = self._ready_touch_receipt()
        self.assertEqual(len(req), 65)
        self.assertEqual(req[0], 2)
        self.assertEqual(req[1:33], self.tx.review_request()[1:])
        reply = self._sign_touch_receipt(req)
        self.assertTrue(self.tx.check_touch_receipt_reply(reply))
        with self.assertRaises(mod.LabTransactionError):
            self.tx.check_touch_receipt_reply(reply)
        with self.assertRaises(mod.LabTransactionError):
            self.tx.touch_receipt_request()

    def test_wrong_device_signing_key_rejected(self):
        req = self._ready_touch_receipt()
        impostor = ec.generate_private_key(ec.SECP256R1())
        with self.assertRaises(mod.LabTransactionError):
            self.tx.check_touch_receipt_reply(self._sign_touch_receipt(req, key=impostor))

    def test_receipt_wrong_session_domain_digest_or_touch_state_rejected(self):
        req = self._ready_touch_receipt()
        variants = (
            self._sign_touch_receipt(req, domain=b"FAKE-TOUCH-RECEIPT-V1\\n"),
            self._sign_touch_receipt(req, session=123457),
            self._sign_touch_receipt(bytes([2])+bytes(32)+req[33:65]),
            self._sign_touch_receipt(req, status=0),
            self._sign_touch_receipt(bytes([2])+req[1:33]+bytes(32)),
        )
        for reply in variants:
            with self.subTest(reply_len=len(reply)):
                with self.assertRaises(mod.LabTransactionError):
                    self.tx.check_touch_receipt_reply(reply)

    def test_tampered_and_malformed_touch_receipt_rejected(self):
        req = self._ready_touch_receipt()
        valid = self._sign_touch_receipt(req)
        tampered = bytearray(valid)
        tampered[-1] ^= 1
        malformed = (
            None, b"", bytes([1]) + valid[1:], bytes([2, 0]) + valid[2:],
            bytes([2, 2]) + valid[2:],
            bytes([2, 1, valid[2]+1]) + valid[3:],
            valid[:-1], valid+b"x", bytes(tampered),
        )
        for reply in malformed:
            with self.subTest(reply=repr(reply)[:32]):
                with self.assertRaises(mod.LabTransactionError):
                    self.tx.check_touch_receipt_reply(reply)

    def test_stale_session_cookie_rejected_before_ha_signs(self):
        reply=bytearray(self.reply)
        reply[4]^=1
        with self.assertRaises(mod.LabTransactionError):
            self.tx.commit_request(bytes(reply))

    def test_public_key_derived_device_id_mismatch_rejected(self):
        reply=bytearray(self.reply)
        reply[26]^=1
        with self.assertRaises(mod.LabTransactionError):
            self.tx.commit_request(bytes(reply))

    def test_cannot_accept_arbitrary_or_malformed_reply(self):
        for invalid in (b"", self.reply[:-1], b"\x01"+self.reply[1:],
                        None, self.reply+b"x"):
            with self.subTest(reply=repr(invalid)[:20]),self.assertRaises(
                    mod.LabTransactionError):
                self.tx.commit_request(invalid)

    def test_tampered_device_signature_rejected(self):
        response=bytearray(self.commit_reply())
        response[2+response[1]-1]^=1
        with self.assertRaises(mod.LabTransactionError):
            self.tx.check_commit(bytes(response))

    def test_device_hash_mismatch_rejected(self):
        response=self.commit_reply(digest_override=b"\x00"*32)
        with self.assertRaises(mod.LabTransactionError):
            self.tx.check_commit(response)

    def test_replay_or_second_commit_rejected(self):
        self.tx.check_commit(self.commit_reply())
        with self.assertRaises(mod.LabTransactionError):
            self.tx.open_request()
        with self.assertRaises(mod.LabTransactionError):
            self.tx.check_commit(b"\x02\x08"+b"x"*40)

    def test_untrusted_result_never_signals_trust(self):
        code=(ROOT/"sec2_tx_v2_lab_client.py").read_text()
        for token in ("nvs_set_blob(", "esp_efuse", "async_save(", "write_flash(",
                      "httpd_register_uri_handler("):
            self.assertNotIn(token,code)
        self.assertIn("LAB-only",code)


if __name__=="__main__":
    unittest.main()
