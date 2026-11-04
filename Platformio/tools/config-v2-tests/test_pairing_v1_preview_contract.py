"""Keep firmware's read-only pairing status byte semantics in sync with HACS.

A preview contract is NOT a cryptographic handshake test.
"""
from __future__ import annotations

import ast
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
HTTP = ROOT / "lib/esp32HalImpl/wifiHandler/config_http.cpp"
CONTRACT = Path(__file__).with_name("pairing_v1_preview_contract.json")
SRP = ROOT / "lib/WaveshareOmote/config_v2_sec2_srp_material.cpp"


class PairingPreviewContractTests(unittest.TestCase):
    def test_get_is_fail_closed_and_matches_wire_fixture(self):
        contract = json.loads(CONTRACT.read_text())
        source = HTTP.read_text()
        self.assertIn(
            'server.on("/api/config/v2/pairing/status", HTTP_GET, handleV2PairingStatusPreview)',
            source,
        )
        section = source.split("void handleV2PairingStatusPreview() {", 1)[1]
        body = section.split("sendJson(200,", 1)[1].split(");", 1)[0]
        fragments = re.findall(r'"(?:\\.|[^"\\])*"', body)
        payload = json.loads("".join(ast.literal_eval(s) for s in fragments))
        self.assertEqual(payload, contract["response"])
        self.assertEqual(contract["api_path"], "/api/config/v2/pairing/status")
        self.assertEqual(contract["method"], "GET")
        self.assertFalse(payload["pairing_window_open"])
        self.assertFalse(payload["session_ready"])
        self.assertFalse(payload["paired"])
        self.assertFalse(payload["deployment_enabled"])
        self.assertEqual(payload["transport_port"], 0)

    def test_real_srp_material_but_no_listener(self):
        source = SRP.read_text()
        self.assertIn("esp_srp_gen_salt_verifier(", source)
        self.assertIn("std::free(generatedSalt)", source)
        self.assertIn("void Ephemeral::clear()", source)
        self.assertNotIn("protocomm_httpd_start(", source)
        self.assertNotIn("server.on(", source)


if __name__ == "__main__":
    unittest.main()
