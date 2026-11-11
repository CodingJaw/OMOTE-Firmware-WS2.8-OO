"""READ-ONLY pre-upload check for the isolated IDF5.5.5 Security2 lab.

The 2026.10.50 Tasmota platform requires PlatformIO Core >=6.2.0.
The platform's Arduino builder calls platform.get_package_dir(
'framework-arduinoespressif32'), which returns None if the framework is
not installed/registered. Path(None) then raises TypeError BEFORE flashing.

Run this with the same PlatformIO executable used for the upload. This
script NEVER installs/deletes packages, flashes hardware or resets NVS.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

MINIMUM_CORE=(6,2,0)
EXPECTED_PLATFORM="2026.10.50"
EXPECTED_ARDUINO="3.3.12"


@dataclass(frozen=True)
class UploadCheck:
    checks: tuple[tuple[str,bool,str], ...]

    @property
    def ready(self)->bool:
        return all(ok for _,ok,_ in self.checks)

    def report(self)->str:
        lines=[f"{'PASS' if ok else 'BLOCK'}: {name}: {details}"
               for name,ok,details in self.checks]
        lines.append("READY FOR A SEPARATE LAB-ONLY UPLOAD COMMAND"
                     if self.ready else
                     "BLOCKED BEFORE UPLOAD; inspect the missing package/core")
        lines.append("No network, flash, NVS, SRP, pairing or config write was attempted.")
        return "\n".join(lines)


def inspect_core_version(output: str)->bool:
    match=re.search(r"PlatformIO Core, version\s+(\d+)\.(\d+)\.(\d+)", output)
    return bool(match and tuple(int(x) for x in match.groups())>=MINIMUM_CORE)


def check_locations(core_version_output: str, packages: Path,
                    platform_manifest: Path)->UploadCheck:
    checks=[]
    def add(name:str, ok:bool, message:str)->None:
        checks.append((name,bool(ok),message))
    add("PlatformIO Core >= 6.2.0",
        inspect_core_version(core_version_output),
        core_version_output.strip() or "version could not be determined")
    try:
        platform=json.loads(platform_manifest.read_text(encoding="utf-8"))
        valid=(platform.get("name")=="espressif32" and
               platform.get("version")==EXPECTED_PLATFORM and
               "arduino" in platform.get("frameworks",{}) and
               "espidf" in platform.get("frameworks",{}))
    except (OSError,ValueError,TypeError):
        valid=False
    add("isolated Tasmota platform",valid,
        f"expected manifest version {EXPECTED_PLATFORM}; file: {platform_manifest}")

    framework=packages/"framework-arduinoespressif32"
    required=framework/"cores"/"esp32"/"Arduino.h"
    manifest=framework/"package.json"
    try:
        meta=json.loads(manifest.read_text(encoding="utf-8"))
        registered=(meta.get("name")=="framework-arduinoespressif32" and
                    meta.get("version","").startswith(EXPECTED_ARDUINO))
    except (OSError,ValueError,TypeError,AttributeError):
        registered=False
    add("Arduino framework installed and recognized",
        required.is_file() and registered,
        f"expected {EXPECTED_ARDUINO}, Arduino.h and package.json in {framework}")

    idf=packages/"framework-espidf"
    try:
        idfmeta=json.loads((idf/"package.json").read_text(encoding="utf-8"))
        idfok=(idfmeta.get("name")=="framework-espidf" and
               str(idfmeta.get("version","")).startswith("3.50505."))
    except (OSError,ValueError,TypeError,AttributeError):
        idfok=False
    add("ESP-IDF 5.5.5 framework present",idfok,
        f"expected framework-espidf 3.50505.x at {idf}")
    return UploadCheck(tuple(checks))


def main()->int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platformio",type=Path,
        default=Path.home()/".platformio/penv/bin/platformio")
    parser.add_argument("--core-dir",type=Path,default=Path.home()/".platformio")
    args=parser.parse_args()
    try:
        process=subprocess.run([str(args.platformio),"--version"],
                              capture_output=True,text=True,timeout=20,
                              check=False)
        output=process.stdout+process.stderr if process.returncode==0 else ""
    except (OSError, subprocess.TimeoutExpired):
        output=""
    result=check_locations(output,args.core_dir/"packages",
                           args.core_dir/"platforms/espressif32/platform.json")
    print(result.report())
    return 0 if result.ready else 2


if __name__=="__main__":
    raise SystemExit(main())
