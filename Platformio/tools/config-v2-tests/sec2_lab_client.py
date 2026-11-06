#!/usr/bin/env python3
"""Manual ESP-IDF Security2 encrypted interoperability probe (LAB ONLY).

No pairing, trusted HA identity, configuration writing or credential storage.
An explicit lab firmware image and local Settings > SEC2 encrypted LAB test >
Start lab gesture are required to open the short-lived TCP 18082 listener.

First use --check-prereqs: this checks Python packages and IDF_PATH LOCALLY,
without contacting the device or consuming persistent Security2 attempts.
The actual PIN is prompted interactively and is never printed or logged.
"""
from __future__ import annotations

import argparse
import asyncio
from getpass import getpass
from hashlib import sha256
import ipaddress
import json
import os
from pathlib import Path
import re
import sys

from sec2_lab_ha_proof import make_candidate_proof, check_lab_reply
from sec2_lab_device_proof import make_challenge, verify_device_proof


class LabSetupError(RuntimeError):
    """Actionable, locally generated prerequisites error (contains no secrets)."""


class LabProbeError(RuntimeError):
    """A failed stage and exception CLASS, never a remote body or PIN."""

    def __init__(self, stage: str, kind: str):
        self.stage = stage
        self.kind = kind
        super().__init__(f"{stage}: {kind}")


_HINTS = {
    "connect": "Check lab firmware is uploaded, Wi-Fi IP is correct and Start lab was tapped on the touchscreen. Port 18082 closes within 120 seconds.",
    "protocol-version": "Check the device displays 'Encrypted LAB test port 18082' and that the lab listener has not expired. Do not use the public port-80 API.",
    "validate-version": "The response does not match this exact lab protocol. Confirm the lab build and client are from the same branch.",
    "local-pin": "Enter exactly the six-digit code shown on the remote screen. The code must never be pasted into logs.",
    "srp-handshake": "Security2 negotiation failed. Stop here: failed SRP frames consume the persistent five-message lab quota, including across reboots.",
    "encrypted-probe": "SRP may have completed but AES-GCM probe verification failed. Do not claim pairing success.",
    "ha-key-proof": "The optional P-256 candidate-key proof failed. It is not production pairing.",
    "device-key-proof": "The experimental device software key proof failed. Do not pin or trust this key.",
}


def _official_provisioning_modules():
    """Load official Espressif tools; do not guess a PlatformIO framework path."""
    configured = os.environ.get("IDF_PATH", "").strip()
    if not configured:
        raise LabSetupError(
            "IDF_PATH is not set. Export it to the root of a compatible "
            "Espressif esp-idf source checkout (not the OMOTE or Platformio folder). "
            "Run --check-prereqs again before contacting the remote."
        )

    root = Path(configured).expanduser().resolve()
    required = (
        "tools/esp_prov/security/security2.py",
        "tools/esp_prov/transport/transport_http.py",
        "components/protocomm/python/session_pb2.py",
        "components/protocomm/python/sec2_pb2.py",
        "components/wifi_provisioning/python/wifi_config_pb2.py",
    )
    if not root.is_dir() or any(not (root / p).is_file() for p in required):
        raise LabSetupError(
            "IDF_PATH does not contain the official Espressif Python "
            "provisioning files. It must point to an ESP-IDF source checkout "
            "containing tools/esp_prov and components/protocomm/python."
        )

    # Espressif's generated protobuf loader reads IDF_PATH at import time.
    os.environ["IDF_PATH"] = str(root)
    toolpath = str(root / "tools/esp_prov")
    if toolpath not in sys.path:
        sys.path.insert(0, toolpath)

    try:
        from security.security2 import Security2
        from transport.transport_http import Transport_HTTP
        from cryptography.hazmat.primitives.asymmetric import ec  # noqa: F401
    except (ImportError, ModuleNotFoundError) as exc:
        # A module name is not a secret; do not print the underlying exception
        # text because third-party errors may embed untrusted data.
        module = getattr(exc, "name", "") or "a required module"
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.]*", module):
            module = "a required module"
        raise LabSetupError(
            f"Python module '{module}' is unavailable. Use a Python environment "
            "with compatible Espressif provisioning dependencies (notably "
            "cryptography and protobuf). No device attempt was made."
        ) from None
    except Exception as exc:
        raise LabSetupError(
            "Official provisioning module initialization failed "
            f"({type(exc).__name__}). Check ESP-IDF/Python dependency versions. "
            "No device attempt was made."
        ) from None

    return Security2, Transport_HTTP


async def probe(
    host: str, port: int, *, ha_proof: bool = False, device_proof: bool = False
) -> None:
    Security2, Transport_HTTP = _official_provisioning_modules()
    stage = "connect"
    tp = None
    try:
        # Official Transport_HTTP maintains the Security2 session Cookie.
        # Do not log headers: they can contain the session identifier.
        tp = Transport_HTTP(f"{host}:{port}")

        stage = "protocol-version"
        version = await tp.send_data("proto-ver", "-")
        stage = "validate-version"
        details = json.loads(version)["prov"]
        if (
            not isinstance(details, dict)
            or details.get("ver") != "omote-sec2-lab-v1"
            or type(details.get("sec_patch_ver")) is not int
            or details["sec_patch_ver"] not in (0, 1)
        ):
            raise ValueError("Unrecognized lab protocol metadata")

        stage = "local-pin"
        pin = getpass("Enter the six-digit code shown on the remote: ")
        if not re.fullmatch(r"[0-9]{6}", pin):
            raise ValueError("Six decimal digits required")

        stage = "srp-handshake"
        sec = Security2(
            sec_patch_ver=details["sec_patch_ver"], username="omote-ha",
            password=pin, verbose=False,
        )
        pin = None
        response = None
        while True:
            request = sec.security_session(response)
            if request is None:
                break
            response = await tp.send_data("prov-session", request)
            if response is None:
                raise ValueError("Empty handshake response")

        stage = "encrypted-probe"
        encrypted = sec.encrypt_data(b"OMOTE-SEC2-PROBE-V1")
        reply = await tp.send_data("omote-probe", encrypted.decode("latin-1"))
        decrypted = sec.decrypt_data(reply.encode("latin-1"))
        if decrypted != b"OMOTE-SEC2-OK-V1":
            raise ValueError("Encrypted probe reply mismatch")
        print("PASS: Security2 SRP6a session and AES-GCM encrypted probe.")

        if ha_proof:
            stage = "ha-key-proof"
            cookie = tp.headers.get("Cookie", "")
            proof = make_candidate_proof(cookie)
            ciphertext = sec.encrypt_data(proof)
            response = await tp.send_data("omote-ha-proof", ciphertext.decode("latin-1"))
            plaintext = sec.decrypt_data(response.encode("latin-1"))
            check_lab_reply(plaintext)
            # Print only the ephemeral PUBLIC key's SHA-256 fingerprint.
            # A user can compare it against the four rows on the remote.
            # The private key and six-digit PIN are never printed or saved.
            ha_fingerprint = sha256(proof[1:66]).hexdigest()
            print("PASS: ephemeral HA P-256 private-key possession proved inside encrypted session.")
            print(f"LAB ONLY candidate HA public-key SHA-256 fingerprint: {ha_fingerprint}")

        if device_proof:
            stage = "device-key-proof"
            cookie = tp.headers.get("Cookie", "")
            challenge = make_challenge()
            ciphertext = sec.encrypt_data(challenge)
            response = await tp.send_data(
                "omote-device-proof", ciphertext.decode("latin-1")
            )
            plaintext = sec.decrypt_data(response.encode("latin-1"))
            fingerprint = verify_device_proof(cookie, challenge, plaintext)
            print("PASS: encrypted lab device P-256 software-key possession proof.")
            print(f"LAB ONLY device public-key SHA-256 fingerprint: {fingerprint}")
            print("This lab software key may be extractable from unencrypted NVS; "
                  "do NOT mark the remote trusted.")
        if ha_proof and device_proof:
            print("Compare the candidate HA fingerprint with the Waveshare screen.")
            print("Touchscreen Review OK is only a LAB acknowledgement; NOT enrollment.")
        print("No production device/HA identity, pairing trust, or configuration writes were tested.")
    except Exception as exc:
        raise LabProbeError(stage, type(exc).__name__) from None
    finally:
        if tp is not None:
            tp.conn.close()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", help="Lab Waveshare IPv4 address")
    ap.add_argument("--port", type=int, default=18082)
    ap.add_argument("--ha-proof", action="store_true",
                    help="Also verify ephemeral HA P-256 key possession (NOT trusted pairing)")
    ap.add_argument("--device-proof", action="store_true",
                    help="Also verify LAB software device-key possession (NOT trusted identity)")
    ap.add_argument("--ack-lab-only", action="store_true",
                    help="Acknowledge this is NOT authenticated OMOTE pairing")
    ap.add_argument("--check-prereqs", action="store_true",
                    help="Check ESP-IDF Python environment only; NO device connection")
    args = ap.parse_args()

    if args.check_prereqs:
        try:
            _official_provisioning_modules()
        except LabSetupError as exc:
            print(f"FAIL [prerequisites]: {exc}", file=sys.stderr)
            return 2
        print("PASS: ESP-IDF Python provisioning prerequisites.")
        print("No network connection, pairing window or SRP attempt was used.")
        return 0

    if not args.host:
        ap.error("--host is required unless --check-prereqs is specified")
    if not args.ack_lab_only:
        ap.error("Use --ack-lab-only after verifying the local lab firmware and UI")
    try:
        ipaddress.IPv4Address(args.host)
    except ipaddress.AddressValueError:
        ap.error("Use a literal IPv4 address for the lab device")
    if not 1 <= args.port <= 65535:
        ap.error("Invalid lab port")

    try:
        asyncio.run(probe(
            args.host, args.port,
            ha_proof=args.ha_proof, device_proof=args.device_proof,
        ))
    except LabSetupError as exc:
        print(f"FAIL [prerequisites]: {exc}", file=sys.stderr)
        return 2
    except LabProbeError as exc:
        print(f"FAIL [{exc.stage}]: {exc.kind}.", file=sys.stderr)
        print(_HINTS.get(exc.stage, "Stop; collect non-secret diagnostic information."),
              file=sys.stderr)
        return 1
    except Exception as exc:
        print(f"FAIL [client]: {type(exc).__name__}. No secrets printed.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
