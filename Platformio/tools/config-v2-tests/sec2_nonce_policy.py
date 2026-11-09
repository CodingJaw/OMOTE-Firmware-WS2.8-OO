"""Offline Security2 AES-GCM nonce safety gate for future OMOTE pairing.

ESP-IDF v5.3.2 uses a fixed AES-GCM nonce in BOTH directions and for
repeated messages under a session key. ESP-IDF v5.4.2 added nonce-counter
patch 1. This audit is intentionally conservative: it does not prove the
entire firmware transport secure, nor authorize enrollment or writes.

The existing manual LAB client is allowed to run patch 0 ONLY for isolated
non-secret cryptographic interoperability experiments. Any future
production pairing transport must fail closed unless BOTH peers negotiate
a supported nonce-safe patch and the client implementation passes review.
"""
from __future__ import annotations

import ast
from dataclasses import dataclass
from pathlib import Path
import re


class NoncePolicyError(ValueError):
    """Unsupported nonce scheme is never an enrollment authorization."""


@dataclass(frozen=True)
class NonceAudit:
    client_counter: bool
    firmware_counter: bool
    client_state: str
    firmware_state: str

    @property
    def suitable_for_further_security_review(self) -> bool:
        # Necessary only; NOT a sufficient production-security test.
        return self.client_counter and self.firmware_counter


def _client_counter_verified(source: str) -> bool:
    """Require patch selection AND a nonce update after encrypt/decrypt."""
    try:
        tree = ast.parse(source)
    except SyntaxError:
        return False
    sec2 = next((node for node in tree.body
                 if isinstance(node, ast.ClassDef) and node.name == "Security2"),
                None)
    if sec2 is None:
        return False
    methods = {node.name: node for node in sec2.body
               if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))}
    if not all(name in methods for name in
               ("__init__", "encrypt_data", "decrypt_data", "_increment_nonce")):
        return False
    if "sec_patch_ver" not in ast.unparse(methods["__init__"]):
        return False
    for name, operation in (
        ("encrypt_data", "self.cipher.encrypt("),
        ("decrypt_data", "self.cipher.decrypt("),
    ):
        body = ast.unparse(methods[name])
        # Fail closed if the nonce isn't advanced AFTER a crypto operation.
        if operation not in body or "self._increment_nonce()" not in body:
            return False
        if body.index(operation) >= body.index("self._increment_nonce()"):
            return False
    return True


def _firmware_counter_verified(source: str) -> bool:
    """Require the official device Security2 patch and post-crypto increment."""
    if re.search(r"\.patch_ver\s*=\s*1\b", source) is None:
        return False
    for name, crypto in (
        ("sec2_encrypt", "mbedtls_gcm_crypt_and_tag("),
        ("sec2_decrypt", "mbedtls_gcm_auth_decrypt("),
    ):
        match = re.search(
            r"static\s+esp_err_t\s+" + name +
            r"\s*\([^)]*\)\s*\{", source, re.S)
        if match is None:
            return False
        body = source[match.end():]
        # All expected methods are followed by another static function.
        # Require the counter update after the *correct* GCM operation.
        match_end = re.search(r"\nstatic\s+", body)
        body = body[:match_end.start()] if match_end else body
        increment = "sec2_gcm_iv_counter_increment(cur_session->iv)"
        if crypto not in body or increment not in body:
            return False
        if body.index(crypto) >= body.index(increment):
            return False
    return True


def audit_source(client_source: str, firmware_source: str) -> NonceAudit:
    client = _client_counter_verified(client_source)
    firmware = _firmware_counter_verified(firmware_source)
    return NonceAudit(
        client_counter=client,
        firmware_counter=firmware,
        client_state="counter-capable" if client else "legacy-or-unverified",
        firmware_state="counter-capable" if firmware else "legacy-or-unverified",
    )


def audit_idf_tree(idf_path: Path) -> NonceAudit:
    root = Path(idf_path).expanduser()
    client_path = root / "tools/esp_prov/security/security2.py"
    firmware_path = root / "components/protocomm/src/security/security2.c"
    try:
        client = client_path.read_text(encoding="utf-8")
        firmware = firmware_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        raise NoncePolicyError("unavailable-or-unreadable-official-idf-source") from exc
    return audit_source(client, firmware)


def require_authenticated_nonce_patch(details: object, *,
                                      require_counter: bool) -> int:
    """Validate a negotiated, device-advertised patch BEFORE SRP messages.

    A successful result does not itself authenticate the advertisement:
    production use must also cryptographically bind the choice and reject
    downgrades before any credentials or trust are written.
    """
    if (type(details) is not dict or
            details.get("ver") != "omote-sec2-lab-v1" or
            type(details.get("sec_patch_ver")) is not int or
            details["sec_patch_ver"] not in (0, 1)):
        raise NoncePolicyError("invalid-security2-version-or-patch")
    patch = details["sec_patch_ver"]
    if require_counter and patch != 1:
        raise NoncePolicyError("legacy-fixed-nonce-security2-rejected")
    return patch


def production_pairing_supported_by_this_lab() -> bool:
    """Explicit kill switch; patch-1 capability alone never grants trust."""
    return False


if __name__ == "__main__":
    raise SystemExit("Run test_sec2_nonce_policy.py; no enrollment CLI is available")
