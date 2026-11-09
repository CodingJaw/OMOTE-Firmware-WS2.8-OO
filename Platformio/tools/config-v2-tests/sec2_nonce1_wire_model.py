"""OFFLINE model of ESP-IDF Security2 patch-1 sequential nonce framing.

Reference for regression tests only. It deliberately does NOT implement
SRP6a, communicate with a device, patch ESP-IDF, or authorize pairing.

The Espressif 5.4+ patch-1 wire nonce is 12 bytes:
    per-SRP-session random prefix[8] || big-endian u32 counter
Both sides start at counter 1 and increment once after each successful
decrypt/encrypt, including alternate request and reply directions. A
single sequential transcript therefore cannot reuse a nonce. If a
message is lost or an extra message appears, peers fail closed.
"""
from __future__ import annotations

from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives.ciphers.aead import AESGCM


class CounterWireError(ValueError):
    """No retry with the same key/IV after a failed or exhausted session."""


class CounterChannel:
    def __init__(self, key: bytes, nonce12: bytes):
        if (type(key) is not bytes or len(key) != 32 or
                type(nonce12) is not bytes or len(nonce12) != 12):
            raise CounterWireError("invalid-AES256-key-or-96bit-IV")
        self._cipher=AESGCM(key)
        self._prefix=nonce12[:8]
        self._counter=int.from_bytes(nonce12[8:],"big")
        if not 1 <= self._counter < 0xFFFFFFFF:
            raise CounterWireError("invalid-or-exhausted-counter")
        self._used: set[bytes] = set()

    @property
    def next_nonce(self) -> bytes:
        if self._counter >= 0xFFFFFFFF:
            raise CounterWireError("nonce-counter-exhausted")
        result=self._prefix+self._counter.to_bytes(4,"big")
        if result in self._used:
            raise CounterWireError("nonce-reuse")
        return result

    def _advance(self,nonce:bytes)->None:
        self._used.add(nonce)
        self._counter+=1

    def encrypt(self,data:bytes)->bytes:
        if type(data) is not bytes:
            raise CounterWireError("invalid-payload")
        nonce=self.next_nonce
        encrypted=self._cipher.encrypt(nonce,data,None)
        self._advance(nonce)
        return encrypted

    def decrypt(self,encrypted:bytes)->bytes:
        if type(encrypted) is not bytes or len(encrypted)<16:
            raise CounterWireError("invalid-ciphertext")
        nonce=self.next_nonce
        try:
            plain=self._cipher.decrypt(nonce,encrypted,None)
        except InvalidTag:
            # A failed authentication makes a sequential shared-counter
            # transcript unusable; terminally poison this test channel.
            self._counter=0xFFFFFFFF
            raise CounterWireError("invalid-GCM-tag-or-nonce") from None
        self._advance(nonce)
        return plain


def replay_is_unauthorized() -> bool:
    return True


def production_pairing_enabled() -> bool:
    return False
