"""Pure-host Secure2 patch-1 AES-GCM nonce/counter interoperability tests.

Never connects to Waveshare, negotiates SRP, flashes firmware or stores trust.
"""
from __future__ import annotations

from hashlib import sha256
import unittest

from sec2_nonce1_wire_model import CounterChannel, CounterWireError, production_pairing_enabled

KEY=sha256(b"OMOTE offline mock-only AES key").digest()
NONCE=b"session8"+(1).to_bytes(4,"big")


class Nonce1WireModelTests(unittest.TestCase):
    def test_multiple_sequential_encrypted_requests_and_responses(self):
        client=CounterChannel(KEY,NONCE)
        device=CounterChannel(KEY,NONCE)
        all_nonces=[]
        for i in range(5):
            first=client.next_nonce
            self.assertEqual(device.next_nonce,first)
            ciphertext=client.encrypt(f"request-{i}".encode())
            self.assertEqual(device.decrypt(ciphertext),f"request-{i}".encode())
            all_nonces.append(first)
            second=device.next_nonce
            self.assertEqual(client.next_nonce,second)
            ciphertext=device.encrypt(f"response-{i}".encode())
            self.assertEqual(client.decrypt(ciphertext),f"response-{i}".encode())
            all_nonces.append(second)
        self.assertEqual(len(all_nonces),len(set(all_nonces)))
        self.assertEqual(all_nonces[0],b"session8"+(1).to_bytes(4,"big"))
        self.assertEqual(all_nonces[-1],b"session8"+(10).to_bytes(4,"big"))
        self.assertFalse(production_pairing_enabled())

    def test_replay_cannot_decrypt_under_next_nonce(self):
        client=CounterChannel(KEY,NONCE)
        server=CounterChannel(KEY,NONCE)
        sent=client.encrypt(b"first")
        self.assertEqual(server.decrypt(sent),b"first")
        with self.assertRaises(CounterWireError):
            server.decrypt(sent)
        with self.assertRaises(CounterWireError):
            server.decrypt(client.encrypt(b"another"))

    def test_invalid_gcm_tag_and_wrong_prefix_fail_closed(self):
        client=CounterChannel(KEY,NONCE)
        first=client.encrypt(b"authentic")
        changed=bytearray(first)
        changed[-1]^=1
        device=CounterChannel(KEY,NONCE)
        with self.assertRaises(CounterWireError):
            device.decrypt(bytes(changed))
        another_prefix=CounterChannel(KEY,b"wrongkey"+(1).to_bytes(4,"big"))
        with self.assertRaises(CounterWireError):
            another_prefix.decrypt(first)
        with self.assertRaises(CounterWireError):
            another_prefix.decrypt(first)

    def test_missing_response_desynchronizes_channel(self):
        client=CounterChannel(KEY,NONCE)
        server=CounterChannel(KEY,NONCE)
        self.assertEqual(server.decrypt(client.encrypt(b"request")),b"request")
        # Peer B sends a response which A never sees.
        server.encrypt(b"lost-response")
        # B expects nonce 3 but A still expects nonce 2.
        with self.assertRaises(CounterWireError):
            server.decrypt(client.encrypt(b"out-of-order-request"))

    def test_counter_exhaustion_never_wraps_to_reused_nonce(self):
        near_end=b"session8"+(0xFFFFFFFE).to_bytes(4,"big")
        a=CounterChannel(KEY,near_end)
        b=CounterChannel(KEY,near_end)
        message=a.encrypt(b"final")
        self.assertEqual(b.decrypt(message),b"final")
        for channel in (a,b):
            with self.assertRaises(CounterWireError):
                channel.encrypt(b"would-wrap")
            with self.assertRaises(CounterWireError):
                channel.decrypt(message)
        for invalid in (0,0xFFFFFFFF):
            with self.assertRaises(CounterWireError):
                CounterChannel(KEY,b"session8"+invalid.to_bytes(4,"big"))

    def test_malformed_inputs_fail_without_accepting_pairing(self):
        for key,nonce in ((b"short",NONCE),(KEY,b"too-short"),(KEY,b""),
                          (None,NONCE),(KEY,None)):
            with self.subTest(key=repr(key)[:12],nonce=repr(nonce)[:12]):
                with self.assertRaises(CounterWireError):
                    CounterChannel(key,nonce)
        channel=CounterChannel(KEY,NONCE)
        for malformed in (None,"text",b"",b"short"):
            with self.assertRaises(CounterWireError):
                channel.decrypt(malformed)
        self.assertFalse(production_pairing_enabled())


if __name__=="__main__":
    unittest.main()
