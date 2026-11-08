"""Static fail-closed guards for dedicated opt-in Security2 transaction v2 LAB."""
from __future__ import annotations

import configparser
import json
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[2]
LIB=ROOT/"lib/WaveshareOmote"
INI=ROOT/"platformio.ini"
TRANSPORT=(LIB/"config_v2_sec2_lab_transport.cpp").read_text()
HEADER=(LIB/"config_v2_sec2_lab_transport.hpp").read_text()
COORD=(LIB/"config_v2_sec2_tx_v2_lab.cpp").read_text()
COORD_HEAD=(LIB/"config_v2_sec2_tx_v2_lab.hpp").read_text()
DEVICE=(LIB/"config_v2_sec2_lab_device_identity.cpp").read_text()
WIRE=json.loads((Path(__file__).with_name(
    "sec2_tx_v2_live_lab_wire_contract.json")).read_text())


class Security2TxV2RuntimeGuardTests(unittest.TestCase):
    def test_new_environment_is_optional_and_inherits_touch_gated_lab(self):
        cfg=configparser.ConfigParser(interpolation=None,strict=False)
        cfg.read(INI)
        normal=cfg["env:waveshare28v2_omote"]
        previous=cfg["env:waveshare28v2_omote_sec2_lab"]
        v2=cfg["env:waveshare28v2_omote_sec2_tx_v2_lab"]
        self.assertEqual(v2["extends"],"env:waveshare28v2_omote_sec2_lab")
        self.assertIn("OMOTE_CONFIG_V2_SEC2_TX_V2_LAB=1",v2["build_flags"])
        self.assertNotIn("OMOTE_CONFIG_V2_SEC2_TX_V2_LAB",normal.get("build_flags",""))
        self.assertNotIn("OMOTE_CONFIG_V2_SEC2_TX_V2_LAB",previous.get("build_flags",""))
        self.assertIn("OMOTE_CONFIG_V2_SEC2_LAB_PROBE=1",previous["build_flags"])

    def test_only_encrypted_registered_protocomm_endpoints(self):
        self.assertIn("kTxV2OpenEndpoint[] = \"omote-tx2-open\"",HEADER)
        self.assertIn("kTxV2CommitEndpoint[] = \"omote-tx2-commit\"",HEADER)
        self.assertIn("kTxV2OpenEndpoint,txV2OpenRequest,this",TRANSPORT)
        self.assertIn("kTxV2CommitEndpoint,txV2CommitRequest,this",TRANSPORT)
        self.assertIn("#if defined(OMOTE_CONFIG_V2_SEC2_TX_V2_LAB)",TRANSPORT)
        self.assertIn("protocomm_set_security(",TRANSPORT)
        self.assertIn("boundedSecurity_.security_req_handler",TRANSPORT)
        self.assertIn("deviceIdentity_",TRANSPORT)
        self.assertNotIn("httpd_register_uri_handler(",COORD)
        self.assertNotIn("handleFsWrite",COORD)

    def test_server_generates_transaction_and_checks_same_session(self):
        self.assertIn("esp_fill_random(candidate.tx.data()",COORD)
        self.assertIn("esp_fill_random(candidate.deviceNonce.data()",COORD)
        self.assertIn("pending_.session!=session",COORD)
        self.assertIn("kTransactionTimeoutMs = 30000",COORD_HEAD)
        self.assertIn("session==completedSession_",COORD)
        self.assertIn("verifyHa(p.haPub.data(),context,request+2,request[1])",COORD)
        self.assertIn("identity.signVerifiedLabDigest(",COORD)
        self.assertLess(COORD.index("!verifyHa(p.haPub.data()"),
                        COORD.index("identity.signVerifiedLabDigest("))
        self.assertIn("txV2_.close();",TRANSPORT)
        self.assertEqual(WIRE["transport"]["transaction_timeout_ms"],30000)

    def test_verified_v2_signatures_feed_existing_physical_touch_review(self):
        # Verification evidence is returned only after HA signature, device
        # signature and transcript have been computed. The transport must
        # reuse the authenticated Security2 session ID and touch-only Review.
        self.assertIn("struct VerifiedPublicKeys",COORD_HEAD)
        self.assertIn("verified = VerifiedPublicKeys{}",COORD)
        self.assertLess(COORD.index("!verifyHa(p.haPub.data()"),
                        COORD.index("verified.valid=true;"))
        self.assertLess(COORD.index("identity.signVerifiedLabDigest("),
                        COORD.index("verified.valid=true;"))
        self.assertLess(COORD.index("hash(transcript,transcriptDigest)"),
                        COORD.index("verified.valid=true;"))
        self.assertIn("verified.ha=p.haPub;",COORD)
        self.assertIn("verified.device=p.devicePub;",COORD)
        self.assertIn("config_v2_sec2_tx_v2_lab::VerifiedPublicKeys verified{}",TRANSPORT)
        self.assertIn("!verified.valid",TRANSPORT)
        self.assertIn("fingerprintOfVerifiedPublic(verified.ha.data())",TRANSPORT)
        self.assertIn("fingerprintOfVerifiedPublic(verified.device.data())",TRANSPORT)
        self.assertIn("self->labReview_.observeVerifiedHa(session,haFp,now)",TRANSPORT)
        self.assertIn("self->labReview_.observeVerifiedDevice(session,deviceFp,now)",TRANSPORT)
        self.assertIn("std::lock_guard<std::mutex> lock(self->reviewMutex_)",TRANSPORT)
        self.assertIn("deviceReview!=config_v2_sec2_lab_mutual_review::Result::TouchRequired",TRANSPORT)
        self.assertIn("std::free(buffer)",TRANSPORT)
        # The existing separate LVGL touchscreen handler alone may acknowledge
        # review. This encrypted network callback must never do so.
        callback=TRANSPORT.split("esp_err_t Probe::txV2CommitRequest(",1)[1].split("#endif",1)[0]
        self.assertNotIn("acknowledgeFromLocalTouch(",COORD+callback)
        self.assertNotIn("trustGranted = true",COORD+callback)
        self.assertNotIn("deploymentEnabled = true",COORD+callback)

    def test_never_authorizes_trust_or_permanent_storage(self):
        for filename,text in (("coordinator",COORD),("header",COORD_HEAD)):
            for forbidden in ("Preferences storage", "nvs_set_blob(",
                              "esp_efuse_write", "esp_ota_begin(",
                              "replaceAfterAuthenticatedPhysicalApproval(",
                              "device_config_write_enabled = true"):
                self.assertNotIn(forbidden,text,filename)
        self.assertIn("signVerifiedLabDigest(",DEVICE)
        self.assertIn("OMOTE_CONFIG_V2_SEC2_TX_V2_LAB",DEVICE)
        self.assertTrue(WIRE["authorization"]["laboratory_only"])
        self.assertFalse(any(v for k,v in WIRE["authorization"].items()
                             if k!="laboratory_only"))

    def test_wire_layouts_and_p256_domains_are_consistent(self):
        self.assertIn('kHaDomain[] = "OMOTE-SEC2-TX-PROOF-HA-V2\\n"',COORD)
        self.assertIn('kDeviceDomain[] = "OMOTE-SEC2-TX-PROOF-DEVICE-V2\\n"',COORD)
        self.assertIn('kDigestDomain[] = "OMOTE-SEC2-TX-TRANSCRIPT-V2\\n"',COORD)
        self.assertIn("kOpenRequestBytes = 1 + 65 + 32",COORD_HEAD)
        self.assertIn("kOpenReplyBytes = 1 + 4 + 4 + 16 + 1 + 8 + 65 + 32",COORD_HEAD)
        self.assertEqual(WIRE["device_id"],"first_8_bytes_SHA256(device_public_sec1_65)")
        self.assertEqual(WIRE["epoch"],1)


if __name__=="__main__":
    unittest.main()
