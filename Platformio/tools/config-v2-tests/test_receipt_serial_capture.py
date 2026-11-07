"""Serial capture utility tests with an in-memory fake serial port (no hardware)."""
from __future__ import annotations

from contextlib import redirect_stdout
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

FILE=Path(__file__).with_name("capture_receipt_device_serial.py")
spec=importlib.util.spec_from_file_location("omote_receipt_capture_only",FILE)
capture_mod=importlib.util.module_from_spec(spec)
sys.modules[spec.name]=capture_mod
spec.loader.exec_module(capture_mod)


class FakeSerial:
    def __init__(self, **kwargs):
        self.args=kwargs
        self.calls=0

    def __enter__(self):
        return self

    def __exit__(self,*args):
        return False

    def read(self, length):
        self.calls+=1
        return b"[RECEIPT-LAB] TEST ONLY\r\n" if self.calls==1 else b""


class ReceiptSerialCaptureTests(unittest.TestCase):
    def test_capture_saves_received_bytes_without_transmitting(self):
        fake=SimpleNamespace(Serial=FakeSerial,SerialException=OSError)
        with tempfile.TemporaryDirectory() as directory:
            destination=Path(directory)/"serial.log"
            clock=iter([0.0,0.1,0.6,1.1])
            out=io.StringIO()
            with patch.dict(sys.modules,{"serial":fake}),patch.object(
                capture_mod.time,"monotonic",side_effect=lambda:next(clock)
            ),redirect_stdout(out):
                result=capture_mod.capture("/dev/ttyACM0",destination,1.0)
            self.assertEqual(result,0)
            self.assertEqual(destination.read_bytes(),
                             b"[RECEIPT-LAB] TEST ONLY\r\n")
            self.assertIn("Capture saved:",out.getvalue())

    def test_rejects_nonserial_input_and_invalid_duration(self):
        fake=SimpleNamespace(Serial=FakeSerial,SerialException=OSError)
        with tempfile.TemporaryDirectory() as directory,patch.dict(sys.modules,{"serial":fake}):
            destination=Path(directory)/"serial.log"
            with patch("sys.stderr",new_callable=io.StringIO):
                self.assertEqual(capture_mod.capture("host:1234",destination,1),2)
                self.assertEqual(capture_mod.capture("/dev/ttyACM0",destination,0),2)
            self.assertFalse(destination.exists())

    def test_does_not_send_commands_or_change_device_identity(self):
        text=FILE.read_text()
        for forbidden in (".write(", "esptool", "nvs_set_blob(", "esp_efuse_write(",
                          "upload_firmware(", "serial.tools.miniterm"):
            # File output uses log.write(received), not serial.write().
            if forbidden==".write(":
                self.assertNotIn("source.write(",text)
            elif forbidden=="esptool":
                self.assertNotIn("import esptool",text)
            else:
                self.assertNotIn(forbidden,text)
        self.assertIn("source.read(4096)",text)


if __name__=="__main__":
    unittest.main()
