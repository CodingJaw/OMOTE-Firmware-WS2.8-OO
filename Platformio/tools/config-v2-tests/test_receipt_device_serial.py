"""Offline physical serial receipt parser acceptance and rejection tests."""
from __future__ import annotations
import importlib.util
from pathlib import Path
import sys
import unittest

MODULE = Path(__file__).with_name("check_receipt_device_serial.py")
spec=importlib.util.spec_from_file_location("omote_receipt_serial_checker",MODULE)
checker=importlib.util.module_from_spec(spec)
sys.modules[spec.name]=checker
spec.loader.exec_module(checker)

BANNER="[RECEIPT-LAB] isolated firmware mbedTLS offline vector verification"
HA="[RECEIPT-LAB] ha valid=1 tamper_refused=1 session_refused=1"
DEV="[RECEIPT-LAB] device valid=1 tamper_refused=1 session_refused=1"
END="[RECEIPT-LAB] FINAL PASS; paired=0 deployment=0 ota=0 efuse=0"
GOOD="\r\n".join(["ESP-ROM:esp32s3",BANNER,HA,DEV,END])


class PhysicalReceiptSerialAcceptance(unittest.TestCase):
    def test_known_good_complete_session(self):
        good,reason=checker.assess(GOOD)
        self.assertTrue(good,reason)
        self.assertIn("NOT PAIRED",reason)

    def test_missing_start_or_incomplete_boot_never_passes(self):
        for log in ("",HA+"\n"+DEV+"\n"+END,
                    BANNER+"\n"+HA+"\n"+END,
                    BANNER+"\n"+DEV+"\n"+END,
                    BANNER+"\n"+HA+"\n"+DEV,
                    GOOD+"\n"+BANNER,
                    GOOD+"\n"+BANNER+"\n"+HA):
            with self.subTest(log=log[-55:]):
                self.assertFalse(checker.assess(log)[0])

    def test_bad_crypto_flags_rejected(self):
        for peer in (HA,DEV):
            for token in ("valid=1","tamper_refused=1","session_refused=1"):
                with self.subTest(peer=peer[14:20],token=token):
                    bad=GOOD.replace(peer,peer.replace(token,token.replace("1","0")))
                    self.assertFalse(checker.assess(bad)[0])

    def test_firmware_fail_and_duplicates_rejected(self):
        for bad in (GOOD.replace("FINAL PASS","FINAL FAIL"),
                    GOOD+"\n"+END,
                    GOOD+"\n"+HA,
                    GOOD+"\n[RECEIPT-LAB] UNSUPPORTED foo",
                    GOOD.replace("deployment=0","deployment=1")):
            with self.subTest(case=bad[-60:]):
                self.assertFalse(checker.assess(bad)[0])

    def test_second_complete_session_is_considered_separately(self):
        valid,reason=checker.assess(GOOD+"\n"+GOOD)
        self.assertTrue(valid,reason)
        self.assertFalse(checker.assess(GOOD+"\n"+GOOD.replace(
            "FINAL PASS","FINAL FAIL"))[0])

    def test_read_only_and_device_isolation(self):
        src=MODULE.read_text()
        for forbidden in ("serial.Serial(", "esptool", "nvs_set_blob(",
                          "efuse_write(", "platformio run", "GenUI."):
            self.assertNotIn(forbidden,src)
        self.assertEqual(checker.MAX_BYTES,1024*1024)


if __name__=="__main__":
    unittest.main()
