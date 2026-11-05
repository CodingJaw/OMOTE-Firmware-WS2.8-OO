"""Safe, offline troubleshooting for the Security2 lab Python client.

No device, socket, firmware, PIN, or NVS counter is touched by these tests.
"""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import AsyncMock, patch

sys.path.insert(0, str(Path(__file__).parent))
import sec2_lab_client as client


class OfflineClientDiagnosticsTests(unittest.TestCase):
    def _run(self, args, idf_path):
        out, err = StringIO(), StringIO()
        with patch.object(sys, "argv", ["sec2_lab_client.py", *args]), \
             patch.dict(os.environ, {"IDF_PATH": idf_path}), \
             redirect_stdout(out), redirect_stderr(err):
            result = client.main()
        return result, out.getvalue(), err.getvalue()

    def test_unset_idf_path_returns_explicit_setup_diagnostic_without_network(self):
        with patch.object(client, "probe", side_effect=AssertionError("network attempted")):
            status, out, err = self._run(["--check-prereqs"], "")
        self.assertEqual(status, 2)
        self.assertIn("FAIL [prerequisites]: IDF_PATH is not set", err)
        self.assertNotIn("Enter the six-digit", out + err)

    def test_wrong_idf_directory_fails_before_connecting(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(client, "probe", side_effect=AssertionError("network attempted")):
                status, out, err = self._run(["--check-prereqs"], directory)
        self.assertEqual(status, 2)
        self.assertIn("does not contain the official", err)
        self.assertNotIn("session=", out + err)

    def test_stage_messages_are_safe_and_do_not_reveal_server_errors(self):
        with patch.object(client, "probe", new_callable=AsyncMock) as mock_probe:
            mock_probe.side_effect = client.LabProbeError("srp-handshake", "RuntimeError")
            status, out, err = self._run(
                ["--host", "10.0.10.121", "--ack-lab-only"], ""
            )
        self.assertEqual(status, 1)
        self.assertIn("FAIL [srp-handshake]: RuntimeError.", err)
        self.assertIn("persistent five-message lab quota", err)
        self.assertNotIn("secret", err.lower())
        self.assertEqual(out, "")

    def test_check_prereqs_does_not_require_host_or_lab_acknowledgment(self):
        with patch.object(client, "_official_provisioning_modules", return_value=(object, object)), \
             patch.object(client, "probe", side_effect=AssertionError("network attempted")):
            status, out, err = self._run(["--check-prereqs"], "")
        self.assertEqual(status, 0)
        self.assertIn("PASS: ESP-IDF Python provisioning prerequisites.", out)
        self.assertIn("No network connection, pairing window or SRP attempt", out)
        self.assertEqual(err, "")


if __name__ == "__main__":
    unittest.main()
