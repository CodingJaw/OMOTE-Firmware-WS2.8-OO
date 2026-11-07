"""Independent C++/OpenSSL vs Python verified Security2 proof transcript parity."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parent
HEADER=ROOT/"enrollment_receipt_verify_draft.hpp"
SOURCE=ROOT/"sec2_verified_proofs_transcript_host.cpp"
PY_SOURCE=ROOT/"sec2_verified_proofs_transcript.py"
FIXTURE=ROOT/"sec2_verified_proofs_transcript_vectors_v1.json"
spec=importlib.util.spec_from_file_location("omote_proofs_crosslang",PY_SOURCE)
model=importlib.util.module_from_spec(spec)
sys.modules[spec.name]=model
spec.loader.exec_module(model)
ARGS=("version","session_id","epoch","tx_id","device_id",
      "ha_public_sec1_hex","device_public_sec1_hex","ha_nonce_hex",
      "device_challenge_hex","ha_proof_signature_der_hex",
      "device_proof_signature_der_hex")


class ProofTranscriptCrossLanguage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.vector=json.loads(FIXTURE.read_text())
        cls.build=tempfile.TemporaryDirectory(prefix="omote-proof-transcript-")
        cls.binary=Path(cls.build.name)/"verify_proofs"
        compiled=subprocess.run(
            ["g++","-std=c++17","-Wall","-Wextra","-Werror","-O1",
             "-DOMOTE_RECEIPT_HOST_OPENSSL=1",
             "-I",str(ROOT),str(SOURCE),"-lcrypto",
             "-o",str(cls.binary)],
            text=True,capture_output=True,check=False)
        if compiled.returncode:
            raise AssertionError("C++ transcript compile failed:\n"+compiled.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def execute(self, **changes):
        doc=dict(self.vector["input"],**changes)
        args=[str(doc[name]) for name in ARGS]
        return subprocess.run([str(self.binary),*args],
                              capture_output=True,text=True,check=False)

    def test_cpp_signed_transcript_matches_python_fixture_exact_bytes(self):
        run=self.execute()
        self.assertEqual(run.returncode,0,run.stderr)
        expected=self.vector["expected"]
        self.assertEqual(run.stdout.splitlines(),[
            expected["transcript_sha256"],expected["ha_key_sha256"],
            expected["device_key_sha256"],expected["canonical_payload_hex"]])
        self.assertEqual(model.derive(self.vector["input"]),expected)

    def test_cpp_rejects_proof_tampering_and_wrong_session(self):
        source=self.vector["input"]
        for k in ("session_id","ha_nonce_hex","device_challenge_hex",
                  "ha_proof_signature_der_hex",
                  "device_proof_signature_der_hex"):
            v=source[k]
            modified=v+1 if k=="session_id" else (
                v[:-1]+("a" if v[-1]!="a" else "b"))
            with self.subTest(field=k):
                self.assertNotEqual(self.execute(**{k:modified}).returncode,0)
                with self.assertRaises(model.TranscriptRejected):
                    model.derive(dict(source,**{k:modified}))

    def test_cpp_context_fields_change_digest_never_authorize(self):
        original=self.vector["expected"]["transcript_sha256"]
        for k,new in (("tx_id","2"*32),("device_id","b"*16),("epoch",3)):
            with self.subTest(field=k):
                result=self.execute(**{k:new})
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertNotEqual(result.stdout.splitlines()[0],original)
        self.assertFalse(any(v for v in self.vector["authorization"].values()))

    def test_host_only_and_not_linked_into_firmware(self):
        cmake=ROOT.parents[1]/"src/CMakeLists.txt"
        self.assertNotIn(SOURCE.name,cmake.read_text())
        self.assertIn("HOST-ONLY",SOURCE.read_text())
        self.assertIn("OMOTE_RECEIPT_HOST_OPENSSL",SOURCE.read_text())
        self.assertIn("OPENSSL",HEADER.read_text())


if __name__=="__main__":
    unittest.main()
