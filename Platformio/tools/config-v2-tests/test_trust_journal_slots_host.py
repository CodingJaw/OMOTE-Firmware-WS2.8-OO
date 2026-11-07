"""Build and exercise the disconnected C++ dual-slot journal on the host.

Requires a C++17 compiler; never opens an ESP32 device or changes flash/NVS.
"""
from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


HERE = Path(__file__).resolve().parent


class DualSlotHostTests(unittest.TestCase):
    def test_cpp_mock_nvs_fault_injection(self):
        compiler = shutil.which("g++") or shutil.which("clang++")
        if not compiler:
            self.skipTest("C++17 compiler unavailable; host test not executed")
        with tempfile.TemporaryDirectory(prefix="omote-nvs-offline-") as temp:
            executable = Path(temp) / "mock-journal"
            build = subprocess.run(
                [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 "-O1", "-I", str(HERE),
                 str(HERE / "trust_journal_slots_host_test.cpp"),
                 "-o", str(executable)],
                capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(build.returncode, 0,
                             "Host-only C++ compiler failed:\n" + build.stderr[-3500:])
            result = subprocess.run([str(executable)], capture_output=True,
                                    text=True, timeout=10)
            self.assertEqual(result.returncode, 0,
                             "Host fake-NVS negative tests failed:\n" + result.stderr[-3500:])

    def test_production_firmware_is_not_linked_to_offline_journal(self):
        source = (HERE / "trust_journal_slots_draft.hpp").read_text()
        self.assertNotIn("nvs_set_blob", source)
        self.assertNotIn("esp_efuse", source)
        self.assertNotIn("esp_restart", source)
        self.assertIn("bool trusted() const { return false; }", source)
        self.assertIn("bool deployment_enabled() const { return false; }", source)


if __name__ == "__main__":
    unittest.main()
