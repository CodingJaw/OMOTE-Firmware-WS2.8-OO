"""Regression: ESP-IDF v5.3.2 internal protocomm HTTPD has eight URI slots.

This is a static LAB startup guard, not a live HTTPD or firmware build test.
The original sec2_lab keeps standalone proof endpoints; signed v2 LAB
disables those redundant endpoints to stay below the internal URI limit.
"""
from pathlib import Path
import re
import unittest

ROOT=Path(__file__).resolve().parents[2]
TRANSPORT=(ROOT/"lib/WaveshareOmote/config_v2_sec2_lab_transport.cpp").read_text()
START=TRANSPORT.split("bool Probe::startFromTrustedLocalTouch(",1)[1].split(
    "void Probe::tick(",1)[0]
MARKER="#if !defined(OMOTE_CONFIG_V2_SEC2_TX_V2_LAB)"
V2_MARKER="#if defined(OMOTE_CONFIG_V2_SEC2_TX_V2_LAB)"
LIMIT=8


class Security2LabHttpdHandlerBudgetTests(unittest.TestCase):
    def test_legacy_endpoints_are_skipped_in_signed_v2_lab_only(self):
        self.assertEqual(START.count(MARKER),1)
        prefix,after=START.split(MARKER,1)
        original,after=after.split("#endif",1)
        self.assertIn("candidate, kProbeEndpoint, probeRequest, this",prefix)
        self.assertIn(
            "candidate, kCandidateProofEndpoint, candidateProofRequest, this",
            original)
        self.assertIn(
            "candidate, kDeviceProofEndpoint, deviceProofRequest, this",
            original)
        self.assertIn(V2_MARKER,after)
        signed=after.split(V2_MARKER,1)[1].split("#endif",1)[0]
        for endpoint in ("kTxV2OpenEndpoint", "kTxV2CommitEndpoint",
                         "kTxV2ReviewEndpoint", "kTxV2ReceiptEndpoint"):
            self.assertRegex(signed,r"candidate,\\s*"+endpoint+r",")
        self.assertNotIn("kCandidateProofEndpoint",signed)
        self.assertNotIn("kDeviceProofEndpoint",signed)

    def test_explicit_budgets_include_security_and_version_slots(self):
        self.assertIn("protocomm_set_security(",START)
        self.assertIn("protocomm_set_version(",START)
        self.assertEqual(2+1+4,7)  # v2: session/version/probe/four v2
        self.assertEqual(2+1+2,5)  # original: session/version/probe/two legacy
        self.assertLessEqual(2+1+4,LIMIT)
        self.assertLessEqual(2+1+2,LIMIT)
        self.assertGreater(2+1+2+4,LIMIT) # broken image: nine routes

    def test_port_and_security_remain_unchanged(self):
        self.assertIn("config.data.config.port = kPort;",START)
        self.assertIn("boundedSecurity_ = protocomm_security2;",START)
        self.assertIn("boundedSecurity_.security_req_handler",START)
        self.assertIn("gatedSecurityRequest",TRANSPORT)
        # Never respond to a route-budget error by granting an insecure
        # fallback or opening unrestricted application writes.
        for forbidden in ("nvs_set_blob(", "httpd_register_uri_handler(",
                          "replaceAfterAuthenticatedPhysicalApproval("):
            self.assertNotIn(forbidden,START)


if __name__=="__main__":
    unittest.main()
