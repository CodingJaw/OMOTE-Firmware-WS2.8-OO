#!/usr/bin/env python3
"""Offline, read-only inspection of a Waveshare ESP32-S3 16 MiB full-flash backup.

Never prints image bytes, configuration, credentials, NVS values or tokens.
It checks readback SHA-256, boot images, and expected partition-table layout.
This is NOT a proof that restoring the backup will work.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import sys

FLASH_BYTES = 0x1000000
TABLE_OFFSET = 0x8000
ENTRY_SIZE = 32
PART_MAGIC = 0x50AA
EXPECTED = {
    "nvs": (0x01, 0x02, 0x9000, 0x5000),
    "otadata": (0x01, 0x00, 0xE000, 0x2000),
    "app0": (0x00, 0x10, 0x10000, 0xA00000),
    "spiffs": (0x01, 0x82, 0xA10000, 0x5E0000),
    "coredump": (0x01, 0x03, 0xFF0000, 0x10000),
}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as fd:
        while True:
            buf = fd.read(65536)
            if not buf:
                break
            digest.update(buf)
    return digest.hexdigest()


def saved_checksum(checksum_path, backup_name):
    if not checksum_path.is_file():
        raise ValueError("checksum file missing")
    lines = checksum_path.read_text(encoding="ascii").strip().splitlines()
    if len(lines) != 1:
        raise ValueError("checksum file must contain exactly one entry")
    parts = lines[0].split(maxsplit=1)
    if len(parts) != 2:
        raise ValueError("invalid SHA-256 checksum file")
    expected, filename = parts
    if len(expected) != 64 or any(c not in "0123456789abcdefABCDEF" for c in expected):
        raise ValueError("invalid SHA-256 digest in checksum file")
    if filename.removeprefix("*") != backup_name:
        raise ValueError("checksum filename does not match selected backup")
    return expected.lower()


def parse_partitions(raw):
    parsed = {}
    for offset in range(0, len(raw), ENTRY_SIZE):
        entry = raw[offset:offset + ENTRY_SIZE]
        if len(entry) < ENTRY_SIZE:
            break
        magic = int.from_bytes(entry[:2], "little")
        if magic in (0xFFFF, 0xEBEB):
            break
        if magic != PART_MAGIC:
            raise ValueError("unrecognized partition table entry at 0x%04x" % (TABLE_OFFSET + offset))
        _magic, typ, subtype, start, size, name, _flags = struct.unpack("<HBBII16sI", entry)
        label = name.partition(b"\x00")[0].decode("ascii", errors="strict")
        if not label or label in parsed:
            raise ValueError("empty or duplicate partition label")
        if start + size > FLASH_BYTES:
            raise ValueError("partition exceeds flash size: " + label)
        parsed[label] = (typ, subtype, start, size)
    if not parsed:
        raise ValueError("partition table empty or not readable")
    return parsed


def inspect(backup):
    errors = []
    if not backup.is_file():
        return ["backup image not found"]
    size = backup.stat().st_size
    if size != FLASH_BYTES:
        return ["expected 16777216 bytes, found " + str(size)]
    try:
        expected_hash = saved_checksum(
            backup.with_name("waveshare-esp32s3-16mb-full-flash.sha256"),
            backup.name,
        )
        actual_hash = sha256(backup)
        if actual_hash != expected_hash:
            errors.append("saved SHA-256 checksum mismatch")
    except (OSError, UnicodeError, ValueError) as exc:
        errors.append("checksum verification failed: " + str(exc))

    with backup.open("rb") as fd:
        fd.seek(0)
        if fd.read(1) != b"\xe9":
            errors.append("bootloader ESP image magic absent at 0x000000")
        fd.seek(0x10000)
        if fd.read(1) != b"\xe9":
            errors.append("app0 ESP image magic absent at 0x010000")
        fd.seek(TABLE_OFFSET)
        raw_table = fd.read(0x1000)
        try:
            actual = parse_partitions(raw_table)
            for label, expected in EXPECTED.items():
                if actual.get(label) != expected:
                    errors.append("partition missing or differs from expected: " + label)
            ranges = sorted((v[2], v[2]+v[3], k) for k, v in actual.items())
            for (_start1, end1, name1), (start2, _end2, name2) in zip(ranges, ranges[1:]):
                if start2 < end1:
                    errors.append("overlapping partitions: " + name1 + " / " + name2)
        except (ValueError, UnicodeError, struct.error) as exc:
            errors.append("partition table inspection failed: " + str(exc))

    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("backup", type=Path, help="private complete 16 MiB flash image")
    args = parser.parse_args()
    problems = inspect(args.backup)
    if problems:
        for issue in problems:
            print("FAIL:", issue)
        print("Backup layout verification incomplete. Do not run destructive tests.")
        return 1
    print("PASS: saved SHA-256 checksum and expected 16 MiB boot/partition layout.")
    print("This is a READ-ONLY integrity and layout check, NOT a proven restoration.")
    print("Keep the full-flash image PRIVATE; it may contain Wi-Fi and HA credentials.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
