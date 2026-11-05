"""Offline LAB-only HA P-256 key-possession wire format.

This creates one ephemeral private key per call, not a durable HA identity.
The resulting proof is valid only for the named Security2 session (cookie)
and a fresh challenge nonce. It does NOT create trust or pair the remote.

Requires the standard Python cryptography package. Never serialize, retain,
or log the private key.
"""
from __future__ import annotations

import os
import re

DOMAIN = b"OMOTE-SEC2-LAB-HA-PROOF-V1"
REPLY = b"OMOTE-LAB-HA-PROOF-OK-V1"


def parse_session_cookie(cookie: str) -> int:
    """Accept only an exact decimal protocomm session cookie, no attributes."""
    if not isinstance(cookie, str) or not re.fullmatch(
        r"session=(?:0|[1-9][0-9]{0,9})", cookie
    ):
        raise ValueError("Missing or malformed Security2 session cookie")
    session = int(cookie[8:])
    if session > 0xFFFFFFFF:
        raise ValueError("Out-of-range Security2 session cookie")
    return session


def make_candidate_proof(cookie: str) -> bytes:
    """Generate ephemeral candidate HA P-256 proof for this encrypted session.

    Not a device identity, not a saved trusted HA key, not an access token.
    """
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec

    session = parse_session_cookie(cookie)
    private = ec.generate_private_key(ec.SECP256R1())
    public = private.public_key().public_bytes(
        serialization.Encoding.X962,
        serialization.PublicFormat.UncompressedPoint,
    )
    if len(public) != 65 or public[0] != 0x04:
        raise ValueError("Unexpected P-256 public-key format")
    nonce = os.urandom(32)
    message = DOMAIN + session.to_bytes(4, "big") + public + nonce
    signature = private.sign(message, ec.ECDSA(hashes.SHA256()))
    if not 8 <= len(signature) <= 80:
        raise ValueError("Unexpected P-256 ECDSA signature length")
    payload = b"\x01" + public + nonce + len(signature).to_bytes(2, "big") + signature
    if len(payload) != 100 + len(signature):
        raise ValueError("Internal candidate proof framing error")
    return payload


def check_lab_reply(reply: bytes) -> None:
    """This only checks an AES-GCM-decrypted unprivileged lab reply."""
    if not isinstance(reply, bytes) or reply != REPLY:
        raise ValueError("Invalid or unauthenticated candidate-proof response")
