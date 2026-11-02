"""Static preview contract regression, not proof of live security."""
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT.parents[1] / "lib/esp32HalImpl/wifiHandler/config_http.cpp"
CONTRACT = json.loads((ROOT / "config_v2_preview_contract.json").read_text())


class TestPreviewContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text()

    def test_contract_never_authorizes_deploy(self):
        self.assertFalse(CONTRACT["deployment_authorized"])
        c = CONTRACT["preview_capabilities"]
        self.assertEqual(c["auth_mode"], "not-implemented")
        self.assertFalse(c["transaction_support"])
        self.assertFalse(c["rollback_support"])
        self.assertTrue(c["write_protection_enabled"])
        self.assertTrue(c["legacy_http_writes_disabled"])

    def test_wire_capabilities_match_fixture(self):
        block = self.source.split("void handleV2CapabilitiesPreview()", 1)[1].split("#endif", 1)[0]
        for key, value in CONTRACT["preview_capabilities"].items():
            literal = json.dumps(value, separators=(",", ":"))
            # C++ concatenated string literals contain escaped JSON quotes.
            expected = ('"' + key + '":' + literal).replace('"', r'\"')
            self.assertIn(expected, block, key)

    def test_read_only_routes_remain_get(self):
        for path, handler in CONTRACT["read_only_routes"].items():
            match = r'server\.on\("' + re.escape(path) + r'",\s*HTTP_GET,\s*' + handler + r'\)'
            self.assertRegex(self.source, match)

    def test_mutations_disabled_in_preview(self):
        preview = self.source.split("#if defined(OMOTE_CONFIG_V2_PREVIEW)", 1)
        # Specifically use route-registration's conditional, not earlier defines.
        registration = self.source.split("void registerRoutes()", 1)[1]
        block = registration.split("#if defined(OMOTE_CONFIG_V2_PREVIEW)", 1)[1].split("#else", 1)[0]
        for item in CONTRACT["blocked_mutations"]:
            route = 'server.on("' + item["path"] + '", ' + item["method"] + ', handleV1MutationDisabled)'
            self.assertIn(route, block)

    def test_no_preview_transaction_route(self):
        registration = self.source.split("void registerRoutes()", 1)[1].split("server.onNotFound", 1)[0]
        self.assertNotRegex(registration, r'server\.on\("/api/config/v2/(?:transactions|pair)')


if __name__ == "__main__":
    unittest.main()
