"""No-network regression checks for Security2 AES-GCM nonce safety gate."""
from __future__ import annotations

import tempfile
from pathlib import Path
import unittest

from sec2_nonce_policy import (
    NoncePolicyError, audit_idf_tree, audit_source,
    require_authenticated_nonce_patch, production_pairing_supported_by_this_lab,
)

CLIENT_LEGACY = """
class Security2:
    def __init__(self, username, password, verbose):
        self.nonce = b"zero"
    def encrypt_data(self, data):
        return self.cipher.encrypt(self.nonce, data, None)
    def decrypt_data(self, data):
        return self.cipher.decrypt(self.nonce, data, None)
"""

CLIENT_COUNTER = """
class Security2:
    def __init__(self, sec_patch_ver, username, password, verbose):
        self.sec_patch_ver = sec_patch_ver
    def _increment_nonce(self):
        self.nonce += b"counter"
    def encrypt_data(self, data):
        ciphertext = self.cipher.encrypt(self.nonce, data, None)
        self._increment_nonce()
        return ciphertext
    def decrypt_data(self, data):
        plaintext = self.cipher.decrypt(self.nonce, data, None)
        self._increment_nonce()
        return plaintext
"""

DEVICE_LEGACY = """
static esp_err_t sec2_encrypt(void *ctx) {
    mbedtls_gcm_crypt_and_tag(ctx);
    return ESP_OK;
}
static esp_err_t sec2_decrypt(void *ctx) {
    mbedtls_gcm_auth_decrypt(ctx);
    return ESP_OK;
}
const protocomm_security_t protocomm_security2 = {
    .ver = 2,
};
"""

DEVICE_COUNTER = """
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


class Security2NoncePolicyTests(unittest.TestCase):
    def test_actual_legacy_pattern_is_rejected_even_with_valid_srp(self):
        result=audit_source(CLIENT_LEGACY,DEVICE_LEGACY)
        self.assertEqual(result.client_state,"legacy-or-unverified")
        self.assertEqual(result.firmware_state,"legacy-or-unverified")
        self.assertFalse(result.suitable_for_further_security_review)

    def test_fully_counter_based_patch_detected_but_never_authorizes(self):
        result=audit_source(CLIENT_COUNTER,DEVICE_COUNTER)
        self.assertEqual(result.client_state,"counter-capable")
        self.assertEqual(result.firmware_state,"counter-capable")
        self.assertTrue(result.suitable_for_further_security_review)
        self.assertFalse(production_pairing_supported_by_this_lab())

    def test_mixed_versions_and_missing_increment_fail_closed(self):
        for client,device in (
            (CLIENT_LEGACY,DEVICE_COUNTER),
            (CLIENT_COUNTER,DEVICE_LEGACY),
            (CLIENT_COUNTER.replace("self._increment_nonce()","pass"),
             DEVICE_COUNTER),
            (CLIENT_COUNTER,
             DEVICE_COUNTER.replace("sec2_gcm_iv_counter_increment(cur_session->iv);","")),
            ("def bad(:",DEVICE_COUNTER),
            (CLIENT_COUNTER,"not actual ESP-IDF"),
        ):
            with self.subTest(client=client[:20],device=device[:20]):
                self.assertFalse(
                    audit_source(client,device).suitable_for_further_security_review)

    def test_fixed_nonce_patch_cannot_pass_future_pairing_gate(self):
        for patch in (0,):
            with self.assertRaises(NoncePolicyError):
                require_authenticated_nonce_patch(
                    {"ver":"omote-sec2-lab-v1","sec_patch_ver":patch},
                    require_counter=True)
            self.assertEqual(require_authenticated_nonce_patch(
                {"ver":"omote-sec2-lab-v1","sec_patch_ver":patch},
                require_counter=False),patch)
        self.assertEqual(require_authenticated_nonce_patch(
            {"ver":"omote-sec2-lab-v1","sec_patch_ver":1},
            require_counter=True),1)

    def test_malformed_patch_advertisement_never_passes(self):
        for patch in (None, True, False, "1", -1, 2, 999):
            with self.subTest(patch=repr(patch)),self.assertRaises(NoncePolicyError):
                require_authenticated_nonce_patch(
                    {"ver":"omote-sec2-lab-v1","sec_patch_ver":patch},
                    require_counter=False)
        for metadata in (None, {}, [], {"ver":"unexpected","sec_patch_ver":1}):
            with self.assertRaises(NoncePolicyError):
                require_authenticated_nonce_patch(metadata,require_counter=True)

    def test_source_checkout_missing_or_legacy_classifies_without_network(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            with self.assertRaises(NoncePolicyError):
                audit_idf_tree(root)
            client=root/"tools/esp_prov/security/security2.py"
            fw=root/"components/protocomm/src/security/security2.c"
            client.parent.mkdir(parents=True)
            fw.parent.mkdir(parents=True)
            client.write_text(CLIENT_LEGACY)
            fw.write_text(DEVICE_LEGACY)
            self.assertFalse(
                audit_idf_tree(root).suitable_for_further_security_review)
            client.write_text(CLIENT_COUNTER)
            fw.write_text(DEVICE_COUNTER)
            self.assertTrue(
                audit_idf_tree(root).suitable_for_further_security_review)


if __name__ == "__main__":
    unittest.main()
