"""Static guards for the opt-in Waveshare mbedTLS build-only receipt probe.

The real cross-toolchain compile is separately requested via PlatformIO.
"""
from pathlib import Path
import configparser
import unittest

BASE=Path(__file__).resolve().parents[2]
PIO=BASE/"platformio.ini"
PROBE=BASE/"src/receipt_mbedtls_compile_probe.cpp"
CMAKE=BASE/"src/CMakeLists.txt"


class WaveshareReceiptBuildGuards(unittest.TestCase):
    def test_explicit_build_only_environment(self):
        settings=configparser.ConfigParser(interpolation=None,strict=False)
        settings.read(PIO)
        name="env:waveshare28v2_omote_sec2_receipt_compile"
        self.assertIn(name,settings)
        environment=settings[name]
        self.assertEqual(environment.get("extends"),"env:waveshare28v2_omote_sec2_lab")
        self.assertIn("+<receipt_mbedtls_compile_probe.cpp>",
                      environment.get("build_src_filter"))
        self.assertIn("+<main.cpp>",environment.get("build_src_filter"))
        self.assertIn("-I tools/config-v2-tests",environment.get("build_flags"))
        self.assertIn("-D OMOTE_RECEIPT_MBEDTLS_COMPILE_PROBE=1",environment.get("build_flags"))
        self.assertIn("#if defined(OMOTE_RECEIPT_MBEDTLS_COMPILE_PROBE)",PROBE.read_text())
        self.assertIn("-DOMOTE_RECEIPT_COMPILE_PROBE=ON",environment.get("board_build.cmake_extra_args"))
        self.assertNotIn("receipt_mbedtls_compile_probe.cpp",
                         settings["env:waveshare28v2_omote"].get("build_src_filter",""))

    def test_probe_has_no_live_registration_or_storage(self):
        source=PROBE.read_text()
        self.assertNotIn("void setup(",source)
        self.assertNotIn("void loop(",source)
        for forbidden in ("nvs_set_blob(", "nvs_open(", "esp_efuse",
                          "httpd_register_uri_handler(", "pairing_start(",
                          "deploy(", "ota_start("):
            self.assertNotIn(forbidden,source)
        self.assertIn("!production_pairing_enabled()",source)
        self.assertIn("!device_write_enabled()",source)

    def test_standalone_serial_vectors_match_pinned_public_fixture(self):
        import json
        vectors=json.loads((BASE/"tools/config-v2-tests/enrollment_receipt_vectors_v1.json").read_text())["vectors"]
        src=(BASE/"src/receipt_mbedtls_device_lab.cpp").read_text()
        settings=configparser.ConfigParser(interpolation=None,strict=False)
        settings.read(PIO)
        env=settings["env:waveshare28v2_omote_sec2_receipt_device_lab"]
        self.assertEqual(env.get("extends"),"env:waveshare28v2_omote")
        self.assertIn("-<*>",env.get("build_src_filter"))
        self.assertIn("+<receipt_mbedtls_device_lab.cpp>",env.get("build_src_filter"))
        self.assertIn("-D OMOTE_RECEIPT_MBEDTLS_DEVICE_LAB=1",env.get("build_flags"))
        self.assertIn("#if defined(OMOTE_RECEIPT_MBEDTLS_DEVICE_LAB)",src)
        self.assertIn("-DOMOTE_RECEIPT_DEVICE_LAB=ON",env.get("board_build.cmake_extra_args"))
        self.assertEqual(env.get("lib_deps"), "")
        self.assertEqual(env.get("lib_ignore"), "")
        for item in vectors:
            self.assertIn(item["public_key_sec1_hex"],src)
            self.assertIn(item["signature_der_hex"],src)
            self.assertIn(item["body"]["ha_key_sha256"],src)
            self.assertIn(item["body"]["device_key_sha256"],src)
        for forbidden in ("nvs_set_blob(", "nvs_open(", "esp_efuse",
                          "WiFi.begin(", "httpd_register_uri_handler(",
                          "Update.begin(", "LittleFS.begin("):
            self.assertNotIn(forbidden,src)
        self.assertIn("paired=0 deployment=0 ota=0 efuse=0",src)
        self.assertIn("delay(3000)",src)
        self.assertIn("report_receipts();",src)

    def test_cmake_selects_only_intended_espidf_sources(self):
        source=CMAKE.read_text()
        self.assertIn("if(OMOTE_RECEIPT_DEVICE_LAB AND OMOTE_RECEIPT_COMPILE_PROBE)",source)
        self.assertIn("if(OMOTE_RECEIPT_DEVICE_LAB)",source)
        self.assertIn("elseif(OMOTE_RECEIPT_COMPILE_PROBE)",source)
        self.assertIn("else()",source)
        self.assertIn('set(app_sources "${CMAKE_CURRENT_LIST_DIR}/receipt_mbedtls_device_lab.cpp")',source)
        self.assertIn('set(app_sources "${CMAKE_CURRENT_LIST_DIR}/main.cpp")',source)
        self.assertIn('"${CMAKE_CURRENT_LIST_DIR}/receipt_mbedtls_compile_probe.cpp"',source)
        self.assertIn("SRCS ${app_sources}",source)
        self.assertIn("REQUIRES mbedtls",source)
        self.assertIn("if(OMOTE_RECEIPT_DEVICE_LAB)\n    idf_component_register(\n        SRCS ${app_sources}\n        REQUIRES mbedtls\n    )",source)
        self.assertIn("REQUIRES espressif__esp_websocket_client espressif__mdns joltwallet__littlefs",source)
        self.assertNotIn("REQUIRES arduino",source)
        self.assertLess(source.index("if(OMOTE_RECEIPT_DEVICE_LAB)"),
                        source.index("elseif(OMOTE_RECEIPT_COMPILE_PROBE)"))
        self.assertNotIn("FILE(GLOB_RECURSE",source)

    def test_existing_lab_environment_does_not_inherit_probe(self):
        settings=configparser.ConfigParser(interpolation=None,strict=False)
        settings.read(PIO)
        env=settings["env:waveshare28v2_omote_sec2_lab"]
        self.assertEqual(env.get("extends"),"env:waveshare28v2_omote")
        self.assertNotIn("receipt_mbedtls_compile_probe",env.get("build_flags",""))


if __name__=="__main__":
    unittest.main()
