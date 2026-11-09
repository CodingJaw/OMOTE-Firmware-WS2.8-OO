"""Offline fake-transport tests for non-SRP laboratory version probe."""
from __future__ import annotations

import json
import unittest

from sec2_nonce1_remote_check import check_remote_version, VersionProbeError


class FakeConnection:
    closed=False

    def close(self):
        self.closed=True


class FakeTransport:
    payload={"prov":{"ver":"omote-sec2-lab-v1","sec_patch_ver":1}}
    last=None
    errors=False

    def __init__(self,target):
        self.target=target
        self.conn=FakeConnection()
        self.calls=[]
        type(self).last=self

    async def send_data(self,endpoint,payload):
        self.calls.append((endpoint,payload))
        if self.errors:
            raise OSError("secret-looking remote error should not leak")
        return json.dumps(self.payload)


class VersionReadOnlySecurity2Tests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        FakeTransport.payload={"prov":{"ver":"omote-sec2-lab-v1","sec_patch_ver":1}}
        FakeTransport.errors=False

    async def test_patch1_advertised_without_srp_or_pin(self):
        version=await check_remote_version("10.0.10.121",18082,FakeTransport)
        self.assertEqual(version,1)
        self.assertEqual(FakeTransport.last.target,"10.0.10.121:18082")
        self.assertEqual(FakeTransport.last.calls,[("proto-ver","-")])
        self.assertTrue(FakeTransport.last.conn.closed)

    async def test_legacy_patch0_rejected_before_srp(self):
        FakeTransport.payload={"prov":{"ver":"omote-sec2-lab-v1","sec_patch_ver":0}}
        with self.assertRaises(VersionProbeError):
            await check_remote_version("10.0.10.121",18082,FakeTransport)
        self.assertEqual(FakeTransport.last.calls,[("proto-ver","-")])
        self.assertTrue(FakeTransport.last.conn.closed)

    async def test_malformed_and_unrecognized_version_rejected(self):
        for payload in (
            {}, [], {"prov":{}},
            {"prov":{"ver":"omote-sec2-lab-v1","sec_patch_ver":True}},
            {"prov":{"ver":"unexpected","sec_patch_ver":1}},
            {"prov":{"ver":"omote-sec2-lab-v1","sec_patch_ver":"1"}},
        ):
            with self.subTest(payload=repr(payload)),self.assertRaises(VersionProbeError):
                FakeTransport.payload=payload
                await check_remote_version("10.0.10.121",18082,FakeTransport)
            self.assertEqual(FakeTransport.last.calls,[("proto-ver","-")])
            self.assertTrue(FakeTransport.last.conn.closed)

    async def test_transport_error_is_sanitized_and_closed(self):
        FakeTransport.errors=True
        with self.assertRaises(VersionProbeError) as caught:
            await check_remote_version("10.0.10.121",18082,FakeTransport)
        self.assertEqual(str(caught.exception),"version-probe-transport-or-format-error")
        self.assertNotIn("secret-looking",str(caught.exception))
        self.assertTrue(FakeTransport.last.conn.closed)


if __name__=="__main__":
    unittest.main()
