"""Read-only audit of the ESP-IDF components installed by PlatformIO.

The python IDF_PATH checkout is NOT automatically the framework used by
PlatformIO. Identify the installed framework component's Security2 source
before planning an isolated, opt-in counter-nonce migration.

This file never connects to the Waveshare, executes PlatformIO, modifies a
package, clears NVS or grants trust. Package-source evidence does not prove
the actual bytes flashed on the physical device.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
from pathlib import Path

from sec2_nonce_policy import _firmware_counter_verified

SECURITY2_RELATIVE = "components/protocomm/src/security/security2.c"


@dataclass(frozen=True)
class FrameworkSourceAudit:
    package: Path
    source_file: Path
    sha256_hex: str
    counter_capable: bool
    error: str = ""


def inspect_framework_packages(platformio_home: Path) -> list[FrameworkSourceAudit]:
    packages = Path(platformio_home).expanduser()/"packages"
    if not packages.is_dir():
        return []
    results: list[FrameworkSourceAudit] = []
    for candidate in sorted(packages.glob("framework-espidf*")):
        if not candidate.is_dir():
            continue
        source_path = candidate/SECURITY2_RELATIVE
        try:
            raw = source_path.read_bytes()
            content = raw.decode("utf-8")
        except (OSError, UnicodeError):
            results.append(FrameworkSourceAudit(
                candidate,source_path,"",False,"source-unavailable"))
            continue
        results.append(FrameworkSourceAudit(
            candidate,source_path,sha256(raw).hexdigest(),
            _firmware_counter_verified(content)))
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--platformio-home",type=Path,default=Path.home()/".platformio",
        help="PlatformIO home directory, default: ~/.platformio")
    args=parser.parse_args()
    results=inspect_framework_packages(args.platformio_home)
    if not results:
        print("UNVERIFIED: no framework-espidf* source package was found.")
        print("No network, device or SRP attempt occurred.")
        return 2
    approved=True
    for result in results:
        state="counter-capable" if result.counter_capable else "legacy-or-unverified"
        print(f"Framework: {result.package}")
        print(f"Security2 source: {result.source_file}")
        print(f"SHA-256: {result.sha256_hex or 'unavailable'}")
        print(f"Nonce implementation: {state}")
        if result.error:
            print(f"Diagnostic: {result.error}")
        if not result.counter_capable:
            approved=False
    print("NOTE: installed package source is NOT proof of what was flashed.")
    print("NOT PAIRED: no changes to trust, NVS, eFuses, device or configuration.")
    return 0 if approved else 2


if __name__=="__main__":
    raise SystemExit(main())
