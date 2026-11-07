#!/usr/bin/env python3
"""Read-only acceptance checker for the isolated ESP32-S3 signed receipt LAB.

Usage: python3 tools/config-v2-tests/check_receipt_device_serial.py receipt.log
Never contacts a device, sends commands, modifies firmware or authorizes trust.
Each boot session is independent: no mixing HA and device results across boots.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re

PREFIX = "[RECEIPT-LAB]"
BANNER = PREFIX + " isolated firmware mbedTLS offline vector verification"
CHECK = re.compile(
    r"^\[RECEIPT-LAB\] (ha|device) valid=([01]) "
    r"tamper_refused=([01]) session_refused=([01])$"
)
FINAL = re.compile(
    r"^\[RECEIPT-LAB\] FINAL (PASS|FAIL); "
    r"paired=0 deployment=0 ota=0 efuse=0$"
)
# Serial lines often contain ESP ROM boot chatter or terminal carriage returns.
# Only exact diagnostic messages are considered evidence.
MAX_BYTES = 1024 * 1024


def assess(text: str) -> tuple[bool, str]:
    if not isinstance(text, str):
        return False, "invalid-input"
    # Use complete sessions only. An earlier PASS does not override a new
    # boot that has started but has not finished its own diagnostics.
    starts = [i for i, line in enumerate(text.splitlines())
              if line.strip() == BANNER]
    if not starts:
        return False, "missing-boot-banner"
    lines = text.splitlines()[starts[-1]:]
    seen: dict[str, tuple[int, int, int]] = {}
    finals = []
    for raw in lines:
        line = raw.strip()
        if line == BANNER:
            # The selected boot banner marks the beginning of this session;
            # it is not an unknown result. A later banner restarts the run
            # and must not allow prior peer results to be reused.
            if seen or finals:
                return False, "duplicate-boot-banner"
            continue
        match = CHECK.fullmatch(line)
        if match:
            role = match.group(1)
            if role in seen:
                return False, "duplicate-peer-result"
            seen[role] = tuple(map(int, match.groups()[1:]))
            continue
        match = FINAL.fullmatch(line)
        if match:
            finals.append(match.group(1))
            continue
        if line.startswith(PREFIX):
            return False, "unknown-diagnostic-line"
    if len(finals) != 1:
        return False, "missing-or-duplicate-final-result"
    if finals[0] != "PASS":
        return False, "firmware-reported-failure"
    if set(seen) != {"ha", "device"}:
        return False, "missing-peer-result"
    for role, bits in seen.items():
        if bits != (1, 1, 1):
            return False, f"{role}-cryptographic-check-failed"
    return True, "ON-DEVICE-OFFLINE-RECEIPT-ACCEPTED (NOT PAIRED)"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="Captured read-only serial output")
    args = parser.parse_args()
    try:
        if not args.log.is_file() or args.log.stat().st_size > MAX_BYTES:
            raise ValueError("log-not-regular-or-too-large")
        content = args.log.read_text(encoding="utf-8", errors="replace")
    except (OSError, ValueError) as exc:
        print(f"FAIL: cannot read serial log ({exc})")
        return 2
    accepted, reason = assess(content)
    print(("PASS: " if accepted else "FAIL: ") + reason)
    print("Production paired=0 deployment=0 ota=0 efuse=0")
    return 0 if accepted else 1


if __name__ == "__main__":
    raise SystemExit(main())
