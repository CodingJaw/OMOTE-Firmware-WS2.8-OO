"""Static guards for the opt-in Waveshare mbedTLS build-only receipt probe.

The real cross-toolchain compile is separately requested via PlatformIO.
"""
from pathlib import Path
import configparser
import unittest

BASE=Path(__file__).resolve().parents[2]
PIO=BASE/"platformio.ini"
PROBE=BASE/"src/receipt_mbedtls_compile_probe.cpp"


class WaveshareReceiptBuildGuards(unittest.TestCase):
    def test_explicit_build_only_environment(self):
        settings=configparser.ConfigParser(interpolation=None,strict=False)
        settings.read(PIO)
        name="env:waveshare28v2_omote_sec2_receipt_compile"
        self.assertIn(name,settings)
        environment=settings[name]
        self.assertEqual(environment.get("extends"),"env:waveshare28v2_omote_sec2_lab")
        self.assertIn("+<receipt_mbedtls_compile_probe.cpp>",
                      environment.get("build_src_filter"))
        self.assertIn("+<main.cpp>",environment.get("build_src_filter"))
        self.assertIn("-I tools/config-v2-tests",environment.get("build_flags"))
        self.assertNotIn("receipt_mbedtls_compile_probe.cpp",
                         settings["env:waveshare28v2_omote"].get("build_src_filter",""))

    def test_probe_has_no_live_registration_or_storage(self):
        source=PROBE.read_text()
        self.assertNotIn("void setup(",source)
        self.assertNotIn("void loop(",source)
        for forbidden in ("nvs_set_blob(", "nvs_open(", "esp_efuse",
                          "httpd_register_uri_handler(", "pairing_start(",
                          "deploy(", "ota_start("):
            self.assertNotIn(forbidden,source)
        self.assertIn("!production_pairing_enabled()",source)
        self.assertIn("!device_write_enabled()",source)

    def test_existing_lab_environment_does_not_inherit_probe(self):
        settings=configparser.ConfigParser(interpolation=None,strict=False)
        settings.read(PIO)
        env=settings["env:waveshare28v2_omote_sec2_lab"]
        self.assertEqual(env.get("extends"),"env:waveshare28v2_omote")
        self.assertNotIn("receipt_mbedtls_compile_probe",env.get("build_flags",""))


if __name__=="__main__":
    unittest.main()
