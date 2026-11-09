"""No-network tests for the opt-in patch1 hardware acceptance preflight."""
from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from sec2_nonce1_hardware_preflight import ENV, inspect
from test_sec2_nonce_policy import CLIENT_COUNTER, DEVICE_COUNTER, CLIENT_LEGACY, DEVICE_LEGACY

INI = """
[env:waveshare28v2_omote_sec2_tx_v2_nonce1_lab]
extends = env:waveshare28v2_omote_sec2_tx_v2_lab
platform = https://github.com/tasmota/platform-espressif32/releases/download/2026.10.50/platform-espressif32.zip
platform_packages =
  framework-arduinoespressif32 @ https://github.com/tasmota/arduino-esp32/releases/download/v3.3.12-1/framework-arduinoespressif32.tar.xz
build_flags =
  ${env:waveshare28v2_omote_sec2_tx_v2_lab.build_flags}
  -D OMOTE_CONFIG_V2_SEC2_NONCE1_LAB=1
lib_deps =
  lovyan03/LovyanGFX@1.2.36
"""


class Nonce1HardwarePreflightTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root=Path(self.tmp.name)
        self.project=root/"Platformio"
        self.project.mkdir()
        self.idf=root/"esp-idf-nonce1"
        (self.project/"platformio.ini").write_text(INI)

    def sources(self, counter=True):
        client=self.idf/"tools/esp_prov/security/security2.py"
        device=self.idf/"components/protocomm/src/security/security2.c"
        client.parent.mkdir(parents=True,exist_ok=True)
        device.parent.mkdir(parents=True,exist_ok=True)
        client.write_text(CLIENT_COUNTER if counter else CLIENT_LEGACY)
        device.write_text(DEVICE_COUNTER if counter else DEVICE_LEGACY)

    def artifacts(self):
        target=self.project/".pio"/"build"/ENV
        target.mkdir(parents=True,exist_ok=True)
        (target/"firmware.bin").write_bytes(bytes(4096))
        (target/"firmware.elf").write_bytes(bytes(8192))

    def test_missing_checkout_and_binaries_block_without_writes(self):
        report=inspect(self.project,None)
        self.assertFalse(report.ready_for_optional_hardware_testing)
        self.assertIn("HARDWARE TEST BLOCKED",report.explain())
        self.assertFalse((self.project/".pio").exists())

    def test_mismatched_legacy_checkout_rejected_even_if_compiled(self):
        self.sources(counter=False)
        self.artifacts()
        report=inspect(self.project,self.idf)
        self.assertFalse(report.ready_for_optional_hardware_testing)
        self.assertIn("legacy-or-unverified",report.explain())

    def test_valid_counter_source_and_both_binaries_passes_preflight(self):
        self.sources()
        self.artifacts()
        report=inspect(self.project,self.idf)
        self.assertTrue(report.ready_for_optional_hardware_testing)
        self.assertIn("READY FOR LAB HARDWARE EVALUATION ONLY",report.explain())
        self.assertIn("NOT PAIRED",report.explain())
        self.assertEqual(report.explain().count("SHA-256"),2)

    def test_only_compiled_elf_is_not_enough(self):
        self.sources()
        self.artifacts()
        (self.project/".pio"/"build"/ENV/"firmware.bin").unlink()
        report=inspect(self.project,self.idf)
        self.assertFalse(report.ready_for_optional_hardware_testing)
        self.assertIn("new-target firmware.bin: missing",report.explain())

    def test_wrong_toolchain_is_rejected(self):
        self.sources()
        self.artifacts()
        (self.project/"platformio.ini").write_text(
            INI.replace("2026.10.50","2025.03.30"))
        report=inspect(self.project,self.idf)
        self.assertFalse(report.ready_for_optional_hardware_testing)
        self.assertIn("isolated pinned PlatformIO target",report.explain())

    def test_stale_lovyangfx_version_blocks_hardware_preflight(self):
        self.sources()
        self.artifacts()
        (self.project/"platformio.ini").write_text(
            INI.replace("lovyan03/LovyanGFX@1.2.36",
                        "lovyan03/LovyanGFX@1.2.0"))
        report=inspect(self.project,self.idf)
        self.assertFalse(report.ready_for_optional_hardware_testing)
        self.assertIn("HARDWARE TEST BLOCKED",report.explain())

    def test_missing_idf_files_and_truncated_firmware_block(self):
        self.artifacts()
        elf=self.project/".pio"/"build"/ENV/"firmware.elf"
        elf.write_bytes(b"bad")
        report=inspect(self.project,self.idf)
        self.assertFalse(report.ready_for_optional_hardware_testing)
        self.assertIn("unreadable source",report.explain())
        self.assertIn("unexpectedly small",report.explain())


if __name__=="__main__":
    unittest.main()
