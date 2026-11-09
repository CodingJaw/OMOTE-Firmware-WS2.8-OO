"""No-network tests for installed PlatformIO ESP-IDF nonce source discovery."""
from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from audit_sec2_platformio_framework import (
    SECURITY2_RELATIVE,inspect_framework_packages,
)

LEGACY="""
static esp_err_t sec2_encrypt(void *ctx) {
    mbedtls_gcm_crypt_and_tag(ctx);
    return ESP_OK;
}
static esp_err_t sec2_decrypt(void *ctx) {
    mbedtls_gcm_auth_decrypt(ctx);
    return ESP_OK;
}
"""
COUNTER="""
static esp_err_t sec2_encrypt(void *ctx) {
    mbedtls_gcm_crypt_and_tag(ctx);
    sec2_gcm_iv_counter_increment(cur_session->iv);
    return ESP_OK;
}
static esp_err_t sec2_decrypt(void *ctx) {
    mbedtls_gcm_auth_decrypt(ctx);
    sec2_gcm_iv_counter_increment(cur_session->iv);
    return ESP_OK;
}
const protocomm_security_t protocomm_security2 = {
  .ver = 2,
  .patch_ver = 1,
};
"""


class FrameworkNonceSourceDiscoveryTests(unittest.TestCase):
    def test_empty_platformio_home_does_not_guess_version(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(inspect_framework_packages(Path(d)),[])

    def test_finds_legacy_counter_and_missing_independently(self):
        with tempfile.TemporaryDirectory() as d:
            base=Path(d)/"packages"
            for name,source in (
                ("framework-espidf",LEGACY),
                ("framework-espidf-counter",COUNTER),
            ):
                path=base/name/SECURITY2_RELATIVE
                path.parent.mkdir(parents=True)
                path.write_text(source)
            (base/"framework-espidf-other").mkdir()
            (base/"framework-arduinoespressif32").mkdir()
            results=inspect_framework_packages(Path(d))
            self.assertEqual([x.package.name for x in results],[
                "framework-espidf","framework-espidf-counter",
                "framework-espidf-other"])
            self.assertFalse(results[0].counter_capable)
            self.assertEqual(len(results[0].sha256_hex),64)
            self.assertTrue(results[1].counter_capable)
            self.assertEqual(len(results[1].sha256_hex),64)
            self.assertFalse(results[2].counter_capable)
            self.assertEqual(results[2].error,"source-unavailable")
            self.assertEqual(results[2].sha256_hex,"")

    def test_ignores_unrelated_platformio_packages(self):
        with tempfile.TemporaryDirectory() as d:
            base=Path(d)/"packages"/"framework-arduinoespressif32"
            base.mkdir(parents=True)
            self.assertEqual(inspect_framework_packages(Path(d)),[])


if __name__ == "__main__":
    unittest.main()
