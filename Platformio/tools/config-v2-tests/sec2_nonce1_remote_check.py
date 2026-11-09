#!/usr/bin/env python3
"""LAB ONLY: read an OMOTE Security2 device's advertised patch without SRP.

The remote version endpoint is UNAUTHENTICATED: this is a low-risk physical
readiness check, not cryptographic verification of the remote's identity.
Never use it for production trust or as a substitute for the full signed
physical-touch lab test. Sends NO prov-session, OPEN, COMMIT or writes.
"""
from __future__ import annotations

import argparse
import asyncio
import ipaddress
import json
import sys

from sec2_lab_client import _official_provisioning_modules, LabSetupError
from sec2_nonce_policy import NoncePolicyError, require_authenticated_nonce_patch


class VersionProbeError(RuntimeError):
    """No PIN or remote response content in this error."""


async def check_remote_version(host: str, port: int, transport_class) -> int:
    """Return only a validated, UNAUTHENTICATED patch-1 advertisement."""
    tp = None
    try:
        tp = transport_class(f"{host}:{port}")
        response = await tp.send_data("proto-ver", "-")
        parsed = json.loads(response)
        if not isinstance(parsed, dict):
            raise VersionProbeError("invalid-lab-version-envelope")
        details = parsed.get("prov")
        try:
            return require_authenticated_nonce_patch(details, require_counter=True)
        except NoncePolicyError:
            raise VersionProbeError("remote-patch1-not-advertised") from None
    except VersionProbeError:
        raise
    except Exception:
        # Avoid printing raw network responses, cookies, private IP headers,
        # PINs or remote exception text.
        raise VersionProbeError("version-probe-transport-or-format-error") from None
    finally:
        if tp is not None:
            tp.conn.close()


def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host", required=True,help="Physical lab Waveshare IPv4 address")
    p.add_argument("--port",type=int,default=18082)
    p.add_argument("--ack-lab-only",action="store_true")
    args=p.parse_args()
    try:
        ipaddress.IPv4Address(args.host)
    except ipaddress.AddressValueError:
        p.error("Use a literal IPv4 host address")
    if not 1<=args.port<=65535:
        p.error("Invalid laboratory port")
    if not args.ack_lab_only:
        p.error("--ack-lab-only is required: this is an unauthenticated lab probe")
    try:
        _,Transport_HTTP=_official_provisioning_modules()
        version=asyncio.run(check_remote_version(args.host,args.port,Transport_HTTP))
    except LabSetupError:
        print("BLOCK: local ESP-IDF Python provisioning prerequisites missing.",file=sys.stderr)
        return 2
    except VersionProbeError as exc:
        print(f"BLOCK: {exc}. No PIN or SRP request was sent.",file=sys.stderr)
        return 2
    print(f"PASS: remote LAB version endpoint advertises Security2 patch {version}.")
    print("UNAUTHENTICATED advertisement only; NOT proof of the flashed firmware.")
    print("No PIN, SRP frame, persistent quota reservation or configuration write was sent.")
    print("NOT PAIRED: full authenticated Security2 and physical-touch test still required.")
    return 0


if __name__=="__main__":
    raise SystemExit(main())
