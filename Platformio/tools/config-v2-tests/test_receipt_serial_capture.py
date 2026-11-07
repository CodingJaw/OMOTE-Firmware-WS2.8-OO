"""No-hardware tests for reconnecting, read-only USB receipt capture."""
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

FILE = Path(__file__).with_name("capture_receipt_device_serial.py")
spec = importlib.util.spec_from_file_location("omote_receipt_capture_only", FILE)
capture_mod = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = capture_mod
spec.loader.exec_module(capture_mod)
BANNER = b"[RECEIPT-LAB] isolated firmware mbedTLS offline vector verification\n"
HA = b"[RECEIPT-LAB] ha valid=1 tamper_refused=1 session_refused=1\n"
DEVICE = b"[RECEIPT-LAB] device valid=1 tamper_refused=1 session_refused=1\n"
FINAL = b"[RECEIPT-LAB] FINAL PASS; paired=0 deployment=0 ota=0 efuse=0\n"
GOOD = BANNER + HA + DEVICE + FINAL


class FakeClock:
    def __init__(self, step=0.2):
        self.now = -step
        self.step = step

    def __call__(self):
        self.now += self.step
        return self.now


class FakeSerial:
    def __init__(self, payload=GOOD, fail_during_read=False, **kwargs):
        self.args=kwargs
        self.payload=payload
        self.fail_during_read=fail_during_read
        self.calls=0

    def __enter__(self):
        return self

    def __exit__(self,*args):
        return False

    def read(self, length):
        self.calls += 1
        if self.fail_during_read:
            raise OSError("USB device disconnected")
        if self.calls == 1:
            return self.payload
        return b""


class ReceiptSerialCaptureTests(unittest.TestCase):
    def capture_with_fake(self, factory, seconds=4):
        fake=SimpleNamespace(Serial=factory,SerialException=OSError)
        with tempfile.TemporaryDirectory() as directory:
            destination=Path(directory)/"serial.log"
            out=io.StringIO()
            with patch.dict(sys.modules,{"serial":fake}),patch.object(
                capture_mod.time,"monotonic",side_effect=FakeClock()
            ),patch.object(capture_mod.time,"sleep",return_value=None),redirect_stdout(out):
                result=capture_mod.capture("/dev/ttyACM0",destination,seconds)
            return result,destination.read_bytes(),out.getvalue()

    def test_capture_valid_full_cycle_without_reset(self):
        result,contents,output=self.capture_with_fake(lambda **kw: FakeSerial(**kw))
        self.assertEqual(result,0)
        self.assertEqual(contents,GOOD)
        self.assertIn("No reset required",output)
        self.assertIn("Capture saved:",output)

    def test_auto_reconnect_after_usb_port_disappears(self):
        calls=[0]
        def factory(**kw):
            calls[0]+=1
            if calls[0] == 1:
                raise OSError("No such file /dev/ttyACM0")
            return FakeSerial(**kw)
        result,contents,output=self.capture_with_fake(factory)
        self.assertEqual(result,0)
        self.assertGreaterEqual(calls[0],2)
        self.assertEqual(contents,GOOD)

    def test_capture_ignores_startup_final_without_banner(self):
        # Opening serial mid-cycle may capture a prior report's final line.
        # Only stop on a full new banner-to-final report.
        payload=FINAL+GOOD
        result,contents,_=self.capture_with_fake(
            lambda **kw: FakeSerial(payload=payload,**kw))
        self.assertEqual(result,0)
        self.assertEqual(contents,payload)

    def test_newline_terminated_report_and_bytearray_detection(self):
        self.assertTrue(capture_mod.has_complete_report(bytearray(GOOD)))
        self.assertTrue(capture_mod.has_complete_report(GOOD.replace(b"\n", b"\r\n")))
        self.assertFalse(capture_mod.has_complete_report(GOOD.rstrip(b"\n")))
        # The previous bug searched for a literal backslash and 'n' rather
        # than the wire's newline byte; guard exact delimiter semantics.
        self.assertFalse(capture_mod.has_complete_report(GOOD.replace(b"\n", b"\\n")))

    def test_cannot_combine_reports_across_new_banner(self):
        incomplete = BANNER + HA + BANNER + DEVICE + FINAL
        self.assertFalse(capture_mod.has_complete_report(incomplete))
        self.assertFalse(capture_mod.has_complete_report(FINAL + HA + DEVICE))
        self.assertFalse(capture_mod.has_complete_report(BANNER + HA + FINAL))
        self.assertTrue(capture_mod.has_complete_report(incomplete + GOOD))

    def test_chunked_serial_reads_complete_only_after_final_newline(self):
        class ChunkedSerial(FakeSerial):
            def read(self, length):
                self.calls += 1
                chunks = [BANNER[:15], BANNER[15:] + HA,
                          DEVICE + FINAL[:-1], FINAL[-1:]]
                return chunks[self.calls-1] if self.calls <= len(chunks) else b""
        result, contents, _ = self.capture_with_fake(lambda **kw: ChunkedSerial(**kw))
        self.assertEqual(result, 0)
        self.assertEqual(contents, GOOD)

    def test_timeout_with_no_complete_report_fails_closed(self):
        result,contents,_=self.capture_with_fake(
            lambda **kw: FakeSerial(payload=b"ESP-ROM boot\n",**kw),seconds=1)
        self.assertEqual(result,1)
        self.assertEqual(contents,b"ESP-ROM boot\n")

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
        for forbidden in ("source.write(", "import esptool", "nvs_set_blob(",
                          "esp_efuse_write(", "upload_firmware(",
                          "serial.tools.miniterm"):
            self.assertNotIn(forbidden,text)
        self.assertIn("source.read(4096)",text)


if __name__=="__main__":
    unittest.main()
