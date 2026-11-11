"""No-network regression for missing PlatformIO Arduino package at upload."""
from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from sec2_nonce1_upload_environment import check_locations,inspect_core_version


class TestUploadEnvironment(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        self.packages=self.root/"packages"
        self.manifest=self.root/"platforms/espressif32/platform.json"
        self.manifest.parent.mkdir(parents=True)
        self.manifest.write_text(json.dumps({
            "name":"espressif32","version":"2026.10.50",
            "frameworks":{"arduino":{},"espidf":{}}}))
        arduino=self.packages/"framework-arduinoespressif32"
        (arduino/"cores/esp32").mkdir(parents=True)
        (arduino/"cores/esp32/Arduino.h").write_text("// mock header\n")
        (arduino/"package.json").write_text(json.dumps({
            "name":"framework-arduinoespressif32","version":"3.3.12"}))
        idf=self.packages/"framework-espidf"
        idf.mkdir()
        (idf/"package.json").write_text(json.dumps({
            "name":"framework-espidf","version":"3.50505.0"}))

    def inspect(self,core="PlatformIO Core, version 6.2.0.post1\n"):
        return check_locations(core,self.packages,self.manifest)

    def test_pio_core_minimum(self):
        self.assertFalse(inspect_core_version("PlatformIO Core, version 6.1.19"))
        self.assertTrue(inspect_core_version("PlatformIO Core, version 6.2.0"))
        self.assertTrue(inspect_core_version("PlatformIO Core, version 6.2.0.post1"))
        self.assertTrue(inspect_core_version("PlatformIO Core, version 6.3.0"))
        self.assertFalse(inspect_core_version("not installed"))
        self.assertFalse(inspect_core_version(""))

    def test_expected_stack_passes_without_modifying_files(self):
        before=list(self.root.rglob("*"))
        report=self.inspect()
        self.assertTrue(report.ready)
        self.assertIn("READY FOR A SEPARATE",report.report())
        self.assertEqual(before,list(self.root.rglob("*")))

    def test_missing_arduino_detected_before_upload(self):
        (self.packages/"framework-arduinoespressif32"/"package.json").unlink()
        report=self.inspect()
        self.assertFalse(report.ready)
        self.assertIn("BLOCK: Arduino framework installed",report.report())
        self.assertIn("No network, flash, NVS",report.report())

    def test_arduino_framework_wrong_version_fails_closed(self):
        package=self.packages/"framework-arduinoespressif32/package.json"
        package.write_text(json.dumps({"name":"framework-arduinoespressif32",
                                       "version":"3.1.3"}))
        self.assertFalse(self.inspect().ready)

    def test_missing_platform_package_and_old_core_rejected(self):
        self.manifest.write_text("{}")
        (self.packages/"framework-espidf/package.json").unlink()
        result=self.inspect("PlatformIO Core, version 6.1.19")
        self.assertFalse(result.ready)
        self.assertIn("BLOCK: isolated Tasmota platform",result.report())
        self.assertIn("BLOCK: ESP-IDF 5.5.5 framework",result.report())

    def test_no_fallback_to_wrong_platform(self):
        self.manifest.write_text(json.dumps({
            "name":"espressif32","version":"2025.03.30",
            "frameworks":{"arduino":{},"espidf":{}}}))
        self.assertFalse(self.inspect().ready)


if __name__=="__main__":
    unittest.main()
