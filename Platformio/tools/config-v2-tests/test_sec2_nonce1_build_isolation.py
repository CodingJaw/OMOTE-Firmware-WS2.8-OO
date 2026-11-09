"""Safety boundary for the isolated patch-1 Waveshare build target.

Offline source checks only. They do not confirm that ESP-IDF 5.5.5 can
compile the entire OMOTE app, or that a physical board is running patch 1.
"""
from __future__ import annotations

import configparser
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT/"platformio.ini"
TRANSPORT = (ROOT/"lib/WaveshareOmote/config_v2_sec2_lab_transport.cpp").read_text()
TARGET = "env:waveshare28v2_omote_sec2_tx_v2_nonce1_lab"
PREVIOUS = "env:waveshare28v2_omote_sec2_tx_v2_lab"
NORMAL = "env:waveshare28v2_omote"


class Nonce1TargetIsolationTests(unittest.TestCase):
    def setUp(self):
        self.cfg=configparser.ConfigParser(interpolation=None,strict=False)
        self.assertTrue(self.cfg.read(CONFIG))

    def test_new_target_uses_matched_tasmota_idf_555_and_arduino_3312(self):
        opt=self.cfg[TARGET]
        self.assertEqual(opt["extends"],PREVIOUS)
        self.assertIn("/2026.10.50/platform-espressif32.zip",opt["platform"])
        self.assertIn(
            "/v3.3.12-1/framework-arduinoespressif32.tar.xz",
            opt["platform_packages"])
        self.assertIn(
            "OMOTE_CONFIG_V2_SEC2_NONCE1_LAB=1",opt["build_flags"])
        self.assertIn(
            "${env:waveshare28v2_omote_sec2_tx_v2_lab.build_flags}",
            opt["build_flags"])

    def test_legacy_targets_and_default_do_not_enable_nonce1(self):
        for env in (NORMAL,PREVIOUS,"env:waveshare28v2_omote_sec2_lab",
                    "env:waveshare28v2"):
            self.assertNotIn(
                "OMOTE_CONFIG_V2_SEC2_NONCE1_LAB",
                self.cfg[env].get("build_flags",""))
        self.assertIn("2025.03.30",self.cfg["env:esp32_Rev5"]["platform"])
        self.assertNotIn("2026.10.50",self.cfg[NORMAL].get("platform",""))
        self.assertEqual(self.cfg["platformio"]["default_envs"],"x64_sim")

    def test_new_listener_rejects_legacy_protocomm_before_side_effects(self):
        start=TRANSPORT.split("bool Probe::startFromTrustedLocalTouch(",1)[1].split(
            "void Probe::tick(",1)[0]
        self.assertIn("#if defined(OMOTE_CONFIG_V2_SEC2_NONCE1_LAB)",start)
        self.assertIn(
            "securityPatchVersion(protocomm_security2, 0) != 1",start)
        self.assertLess(
            start.index("securityPatchVersion(protocomm_security2, 0) != 1"),
            start.index("deviceIdentity_.prepareFromTrustedLocalTouch()"))
        self.assertLess(
            start.index("securityPatchVersion(protocomm_security2, 0) != 1"),
            start.index("material_.generate(pin, len)"))
        self.assertIn("versionNumber == 2 && patchNumber == 1",start)
        self.assertIn("(patchNumber == 0 || patchNumber == 1)",start)
        self.assertIn("!defined(OMOTE_CONFIG_V2_SEC2_TX_V2_LAB)",TRANSPORT)
        self.assertIn('Nonce1 lab must extend the isolated signed Security2 v2 laboratory target',TRANSPORT)

    def test_no_production_pairing_or_writes_enabled(self):
        flags=self.cfg[TARGET]["build_flags"]
        for forbidden in ("OMOTE_PAIRING_ENABLED","OMOTE_ENABLE_DEPLOYMENT",
                          "OMOTE_OTA_ENABLED","ESP_EFUSE_WRITE"):
            self.assertNotIn(forbidden,flags)
        self.assertNotIn("replaceAfterAuthenticatedPhysicalApproval(",TRANSPORT)


if __name__=="__main__":
    unittest.main()
