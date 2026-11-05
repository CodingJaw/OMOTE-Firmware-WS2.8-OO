"""Verify an encrypted LAB-only Waveshare software device identity proof.

Not a production device attestation: ordinary NVS flash can be extracted or
cloned if hardware-backed storage and flash/NVS encryption are not enabled.
No Home Assistant pairing or trusted device enrollment is performed.
"""
from __future__ import annotations

from hashlib import sha256
import re
from secrets import token_bytes

DOMAIN = b"OMOTE-SEC2-LAB-DEVICE-PROOF-V1"


def make_challenge() -> bytes:
    return b"\x01" + token_bytes(32)


def parse_session_cookie(cookie: str) -> int:
    if not isinstance(cookie, str) or not re.fullmatch(
        r"session=(?:0|[1-9][0-9]{0,9})", cookie
    ):
        raise ValueError("Invalid Security2 session cookie")
    value = int(cookie[8:])
    if value > 0xFFFFFFFF:
        raise ValueError("Security2 session ID out of range")
    return value


def verify_device_proof(cookie: str, challenge: bytes, response: bytes) -> str:
    """Return a SHA-256 fingerprint after verifying the actual P-256 signature.

    The fingerprint is informational only until it has been pinned using an
    independently authenticated channel; never infer trusted device identity.
    """
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec

    session = parse_session_cookie(cookie)
    if (
        not isinstance(challenge, bytes) or len(challenge) != 33
        or challenge[0] != 1
        or not isinstance(response, bytes)
        or len(response) < 1 + 65 + 2 + 8
        or len(response) > 1 + 65 + 2 + 80
        or response[0] != 1 or response[1] != 4
    ):
        raise ValueError("Malformed LAB device proof")
    public = response[1:66]
    signature_length = int.from_bytes(response[66:68], "big")
    if not 8 <= signature_length <= 80 or len(response) != 68 + signature_length:
        raise ValueError("Malformed LAB device proof signature length")

    key = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), public)
    signed = DOMAIN + session.to_bytes(4, "big") + challenge[1:] + public
    key.verify(response[68:], signed, ec.ECDSA(hashes.SHA256()))
    return sha256(public).hexdigest()
