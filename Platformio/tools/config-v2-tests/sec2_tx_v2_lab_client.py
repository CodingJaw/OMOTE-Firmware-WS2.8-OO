"""Manual LAB-only client framing for a mutually signed live Security2 v2 tx.

Must be invoked ONLY inside the authenticated and encrypted protocomm
Security2 session, after verifying a server reply's cookie matches the
locally observed cookie. This is neither HA pairing nor trusted attestation.
"""
from __future__ import annotations

from hashlib import sha256
from secrets import token_bytes

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

from sec2_lab_device_proof import parse_session_cookie
from sec2_mutual_transaction_proof_v2 import (
    HA_DOMAIN, MutualProofRejected, canonical_context, derive_bound,
)

OPEN_LENGTH = 1 + 4 + 4 + 16 + 1 + 8 + 65 + 32
MAX_COMMIT_LENGTH = 2 + 80 + 32


class LabTransactionError(ValueError):
    pass


class LabV2Transaction:
    def __init__(self, authenticated_cookie: str):
        self.session_id = parse_session_cookie(authenticated_cookie)
        if not self.session_id:
            raise LabTransactionError("invalid-zero-security2-session")
        self._ha_key = ec.generate_private_key(ec.SECP256R1())
        self._ha_pub = self._ha_key.public_key().public_bytes(
            serialization.Encoding.X962,
            serialization.PublicFormat.UncompressedPoint,
        )
        self._nonce = token_bytes(32)
        self.context = None
        self._ha_sig = None
        self._verified_digest = None

    def review_request(self) -> bytes:
        """Read-only LAB status check for this verified Security2 transcript."""
        if self._verified_digest is None:
            raise LabTransactionError("review-requires-verified-commit")
        return b"\\x02" + self._verified_digest

    def check_review_reply(self, payload: bytes) -> bool:
        """Return True only after physical Review OK, never an enrollment grant."""
        if self._verified_digest is None:
            raise LabTransactionError("review-requires-verified-commit")
        if (type(payload) is not bytes or len(payload) != 34 or
                payload[0] != 2 or payload[1] not in (0, 1) or
                payload[2:] != self._verified_digest):
            raise LabTransactionError("invalid-session-bound-review-reply")
        return payload[1] == 1

    def verified_public_fingerprints(self) -> dict[str, str]:
        """Public fingerprint comparison only AFTER verified device signature.

        The ephemeral HA private key is discarded by check_commit(); neither
        fingerprint represents persistent trust, pairing, or approval.
        """
        if self._ha_key is not None or self._ha_sig is not None or self.context is None:
            raise LabTransactionError("fingerprints-require-verified-commit")
        return {
            "ha": sha256(self._ha_pub).hexdigest(),
            "device": sha256(
                bytes.fromhex(self.context["device_public_sec1_hex"])
            ).hexdigest(),
        }

    def open_request(self) -> bytes:
        if self.context is not None:
            raise LabTransactionError("open-repeated")
        return b"\x02" + self._ha_pub + self._nonce

    def commit_request(self, encrypted_open_reply: bytes) -> bytes:
        if self.context is not None or self._ha_sig is not None:
            raise LabTransactionError("transaction-already-open")
        response = encrypted_open_reply
        if (type(response) is not bytes or len(response) != OPEN_LENGTH or
                response[0] != 2):
            raise LabTransactionError("invalid-open-reply")
        session_id = int.from_bytes(response[1:5], "big")
        epoch = int.from_bytes(response[5:9], "big")
        tx = response[9:25]
        did_len = response[25]
        if did_len != 8 or not epoch or session_id != self.session_id:
            raise LabTransactionError("wrong-authenticated-session-or-device-id")
        device_id = response[26:34]
        device_pub = response[34:99]
        challenge = response[99:131]
        if device_id != sha256(device_pub).digest()[:8]:
            raise LabTransactionError("device-id-public-key-mismatch")
        observed = {
            "version": 2,
            "session_id": self.session_id,
            "epoch": epoch,
            "tx_id": tx.hex(),
            "device_id": device_id.hex(),
            "ha_public_sec1_hex": self._ha_pub.hex(),
            "device_public_sec1_hex": device_pub.hex(),
            "ha_nonce_hex": self._nonce.hex(),
            "device_challenge_hex": challenge.hex(),
        }
        # Exact canonical format including both keys and transaction metadata
        # is signed by HA. A malicious device cannot change any byte later.
        context_bytes = canonical_context(observed)
        signature = self._ha_key.sign(
            HA_DOMAIN + context_bytes, ec.ECDSA(hashes.SHA256()))
        if not 8 <= len(signature) <= 80:
            raise LabTransactionError("invalid-local-signature")
        self.context = observed
        self._ha_sig = signature
        return b"\x02" + bytes([len(signature)]) + signature

    def check_commit(self, encrypted_commit_reply: bytes) -> dict:
        if self.context is None or self._ha_sig is None:
            raise LabTransactionError("missing-live-context")
        response = encrypted_commit_reply
        if (type(response) is not bytes or
                not 2 + 8 + 32 <= len(response) <= MAX_COMMIT_LENGTH or
                response[0] != 2):
            raise LabTransactionError("invalid-commit-reply")
        sig_len = response[1]
        if not 8 <= sig_len <= 80 or len(response) != 2 + sig_len + 32:
            raise LabTransactionError("invalid-commit-length")
        envelope = {
            "context": self.context,
            "ha_signature_der_hex": self._ha_sig.hex(),
            "device_signature_der_hex": response[2:2 + sig_len].hex(),
        }
        try:
            bound = derive_bound(envelope, observed=self.context)
        except MutualProofRejected:
            raise LabTransactionError("invalid-mutual-signatures") from None
        if bytes.fromhex(bound["session_transcript_sha256"]) != response[-32:]:
            raise LabTransactionError("device-session-transcript-digest-mismatch")
        # Single-use verification, never a trust state. Drop local signing key.
        self._verified_digest = response[-32:]
        self._ha_key = None
        self._ha_sig = None
        return bound
