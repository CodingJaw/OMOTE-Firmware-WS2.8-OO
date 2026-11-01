"""Synthetic offline tests; never touches an ESP32, flash or serial port."""
import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
import importlib.util

MODULE = Path(__file__).with_name("inspect-waveshare-backup.py")
spec = importlib.util.spec_from_file_location("omote_backup_inspector", MODULE)
inspector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspector)


class BackupInspectorTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / "waveshare-esp32s3-16mb-full-flash.bin"
        with self.path.open("wb") as f:
            f.truncate(inspector.FLASH_BYTES)
        with self.path.open("r+b") as f:
            f.seek(0)
            f.write(b"\xe9")
            f.seek(0x10000)
            f.write(b"\xe9")
            f.seek(inspector.TABLE_OFFSET)
            for name, (typ, subtype, off, size) in inspector.EXPECTED.items():
                f.write(struct.pack("<HBBII16sI", inspector.PART_MAGIC, typ, subtype,
                                    off, size, name.encode("ascii").ljust(16, b"\x00"), 0))
            f.write(b"\xff" * 32)
        self.rehash()

    def rehash(self):
        digest = inspector.sha256(self.path)
        self.path.with_name("waveshare-esp32s3-16mb-full-flash.sha256").write_text(
            digest + "  " + self.path.name + "\n", encoding="ascii"
        )

    def test_expected_layout_passes(self):
        self.assertEqual(inspector.inspect(self.path), [])

    def test_corrupt_flash_detected(self):
        with self.path.open("r+b") as f:
            f.seek(0xA10000)
            f.write(b"changed content")
        self.assertIn("saved SHA-256 checksum mismatch", inspector.inspect(self.path))

    def test_wrong_partition_detected_even_with_good_checksum(self):
        with self.path.open("r+b") as f:
            f.seek(inspector.TABLE_OFFSET + 4)
            f.write((0xB000).to_bytes(4, "little"))
        self.rehash()
        self.assertTrue(any("partition missing" in x for x in inspector.inspect(self.path)))

    def test_wrong_bootloader_detected_even_with_good_checksum(self):
        with self.path.open("r+b") as f:
            f.seek(0)
            f.write(b"\xff")
        self.rehash()
        self.assertTrue(any("bootloader" in x for x in inspector.inspect(self.path)))

    def test_missing_checksum_rejected(self):
        self.path.with_name("waveshare-esp32s3-16mb-full-flash.sha256").unlink()
        self.assertTrue(any("checksum" in x for x in inspector.inspect(self.path)))

    def test_short_flash_rejected(self):
        with self.path.open("r+b") as f:
            f.truncate(1024)
        self.assertTrue(any("expected 16777216" in x for x in inspector.inspect(self.path)))


if __name__ == "__main__":
    unittest.main()
