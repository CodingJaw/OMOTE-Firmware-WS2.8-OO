"""Offline acceptance preflight for experimental Security2 patch-1 Waveshare.

No network access, PIN entry, flashing, file modification, or configuration
writes. This verifies *source and artifact prerequisites*, not whether the
device has actually booted the image or negotiated authenticated patch 1.
Never use this result to authorize production pairing.
"""
from __future__ import annotations

import argparse
import configparser
from dataclasses import dataclass
from hashlib import sha256
import os
from pathlib import Path
from typing import Optional

from sec2_nonce_policy import NoncePolicyError, audit_idf_tree

ENV = "waveshare28v2_omote_sec2_tx_v2_nonce1_lab"
PINNED_PLATFORM = "2026.10.50/platform-espressif32.zip"
PINNED_ARDUINO = "v3.3.12-1/framework-arduinoespressif32.tar.xz"


@dataclass(frozen=True)
class Preflight:
    ready_for_optional_hardware_testing: bool
    checks: tuple[tuple[str, bool, str], ...]

    def explain(self) -> str:
        lines = []
        for name, ok, details in self.checks:
            lines.append(f"{'PASS' if ok else 'BLOCK'}: {name}: {details}")
        lines.append(
            "READY FOR LAB HARDWARE EVALUATION ONLY; NOT PAIRED"
            if self.ready_for_optional_hardware_testing
            else "HARDWARE TEST BLOCKED; no network, flash or SRP attempt made"
        )
        lines.append(
            "This checks local files, NOT binary provenance or the flashed "
            "device's runtime Security2 patch."
        )
        return "\n".join(lines)


def inspect(project: Path, idf: Optional[Path]) -> Preflight:
    project = Path(project).expanduser()
    checks: list[tuple[str, bool, str]] = []
    def record(name: str, ok: bool, details: str) -> None:
        checks.append((name, bool(ok), details))

    ini = project / "platformio.ini"
    cfg = configparser.ConfigParser(interpolation=None)
    try:
        cfg.read(ini, encoding="utf-8")
        section = cfg["env:" + ENV]
        platform = section.get("platform", "")
        packages = section.get("platform_packages", "")
        flags = section.get("build_flags", "")
        ok = (
            PINNED_PLATFORM in platform
            and PINNED_ARDUINO in packages
            and "OMOTE_CONFIG_V2_SEC2_NONCE1_LAB=1" in flags
            and section.get("extends") ==
                "env:waveshare28v2_omote_sec2_tx_v2_lab"
        )
        record("isolated pinned PlatformIO target", ok,
               "expected Tasmota 2026.10.50 / Arduino 3.3.12-1 / IDF 5.5.5")
    except (OSError, ValueError, KeyError, configparser.Error):
        record("isolated pinned PlatformIO target", False,
               "missing or invalid platformio.ini/target")

    if idf is None:
        record("patched Python ESP-IDF checkout", False,
               "IDF_PATH not set; use a separate ESP-IDF v5.5.5 sparse checkout")
    else:
        root = Path(idf).expanduser()
        try:
            audit = audit_idf_tree(root)
            ok = audit.client_counter and audit.firmware_counter
            detail = ("Python: " + audit.client_state +
                      "; IDF source: " + audit.firmware_state)
            record("patched Python ESP-IDF checkout", ok, detail)
        except NoncePolicyError:
            record("patched Python ESP-IDF checkout", False,
                   "missing/incomplete/unreadable source; do not use legacy tree")

    build = project / ".pio" / "build" / ENV
    for name in ("firmware.bin", "firmware.elf"):
        artifact = build / name
        try:
            if not artifact.is_file():
                raise OSError("missing")
            content = artifact.read_bytes()
            if len(content) < 1024:
                raise OSError("unexpectedly short")
            digest = sha256(content).hexdigest()
            record("new-target " + name, True,
                   f"present ({len(content)} bytes), SHA-256 {digest}")
        except OSError:
            record("new-target " + name, False,
                   "missing, unreadable or unexpectedly small; compile target first")

    return Preflight(
        ready_for_optional_hardware_testing=all(x[1] for x in checks),
        checks=tuple(checks),
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path,
                        default=Path(__file__).resolve().parents[2])
    parser.add_argument("--idf-path", type=Path,
                        default=Path(os.environ["IDF_PATH"])
                        if os.environ.get("IDF_PATH") else None)
    args = parser.parse_args()
    report = inspect(args.project, args.idf_path)
    print(report.explain())
    return 0 if report.ready_for_optional_hardware_testing else 2


if __name__ == "__main__":
    raise SystemExit(main())
