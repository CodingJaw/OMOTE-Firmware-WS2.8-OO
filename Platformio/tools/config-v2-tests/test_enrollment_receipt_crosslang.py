"""Build the disconnected C++ verifier and cross-check pinned Python vectors.

OpenSSL is host-test-only. The ESP-IDF mbedTLS branch is not board-compiled
by this test and must not be claimed hardware-verified.
"""
from __future__ import annotations

import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent
VECTORS = ROOT / "enrollment_receipt_vectors_v1.json"
CPP = ROOT / "enrollment_receipt_verify_host.cpp"


def argv(exe, item, **changed):
    b = dict(item["body"], **changed)
    return [str(exe), item["public_key_sec1_hex"],
            item["signature_der_hex"], b["tx_id"], b["device_id"],
            str(b["epoch"]), b["ha_key_sha256"], b["device_key_sha256"],
            b["session_transcript_sha256"], b["nonce"], b["signer"],
            b["phase"], b["version"]]


class ReceiptCrossLanguageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.vectors = json.loads(VECTORS.read_text())["vectors"]
        cls.compiler = shutil.which("g++") or shutil.which("clang++")

    def test_pinned_vectors_are_well_formed(self):
        self.assertEqual(len(self.vectors), 2)
        self.assertEqual({v["body"]["signer"] for v in self.vectors},
                         {"ha", "device"})
        for v in self.vectors:
            self.assertEqual(len(bytes.fromhex(v["public_key_sec1_hex"])), 65)
            self.assertTrue(v["canonical_payload_hex"].startswith(
                "4f4d4f54452d454e524f4c4c4d454e542d524543454950542d56310a"))

    def test_host_canonical_and_real_ecdsa_verification(self):
        if not self.compiler:
            self.skipTest("C++17 compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="omote-receipt-host-") as directory:
            exe = Path(directory) / "receipt-check"
            build = subprocess.run(
                [self.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 "-O1", str(CPP), "-lcrypto", "-o", str(exe)],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stderr[-4000:])
            for v in self.vectors:
                with self.subTest(signer=v["body"]["signer"]):
                    good = subprocess.run(argv(exe, v), capture_output=True,
                                          text=True, timeout=10)
                    self.assertEqual(good.returncode, 0, good.stderr)
                    self.assertEqual(good.stdout.strip(),
                                     v["canonical_payload_hex"])
                    for field, altered in (
                        ("phase", "peer_confirmed" if
                         v["body"]["phase"] != "peer_confirmed" else "prepare"),
                        ("tx_id", "22"*16),
                        ("device_id", "b2"*8),
                        ("nonce", "33"*16),
                        ("session_transcript_sha256", "cd"*32),
                    ):
                        with self.subTest(field=field):
                            bad = subprocess.run(argv(exe, v, **{field: altered}),
                                                 capture_output=True, timeout=10)
                            self.assertEqual(bad.returncode, 2)
                    for failure in ("bad-session", "bad-peer"):
                        bad = subprocess.run(argv(exe, v)+[failure],
                                             capture_output=True, timeout=10)
                        self.assertEqual(bad.returncode, 2)

    def test_no_firmware_runtime_link_or_authorization(self):
        hdr = (ROOT / "enrollment_receipt_verify_draft.hpp").read_text()
        self.assertIn("bool production_pairing_enabled() { return false; }", hdr)
        self.assertIn("bool device_write_enabled() { return false; }", hdr)
        self.assertNotIn("nvs_set_blob(", hdr)
        self.assertNotIn("esp_efuse", hdr)
        self.assertNotIn("httpd_register_uri_handler", hdr)


if __name__ == "__main__":
    unittest.main()
