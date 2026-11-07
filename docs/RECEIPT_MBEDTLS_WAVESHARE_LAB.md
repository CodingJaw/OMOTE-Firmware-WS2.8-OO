# Waveshare ESP32-S3 P-256 receipt verifier — isolated mbedTLS acceptance

Date: 2026-11-07. Branch: `feature/config-v2-transactional`.

## Existing verified results

- HACS Python signed-vector tests: 3/3 PASS (user, 2026-11-07).
- Firmware C++17/OpenSSL host signed-vector tests: 3/3 PASS
  (user, 2026-11-07).
- Actual Waveshare ESP-IDF/mbedTLS receipt verifier build and on-device
  execution: **NOT VERIFIED YET**. Do not confuse these stages.

## Two new opt-in build targets

Both use the existing Waveshare Arduino + ESP-IDF toolchain, so the
firmware's mbedTLS code path compiles rather than the host OpenSSL path.

Both new C++ source files are additionally protected by separate
compile-time macros. Normal Waveshare, Rev1/Rev5 and simulator builds do
not enable these probes, even if a build environment scans the source tree.

### 1. Compile-only integration check — safe, no flash

From the firmware repository:

```bash
git checkout feature/config-v2-transactional
git pull --ff-only origin feature/config-v2-transactional
cd Platformio
~/.platformio/penv/bin/platformio run -e waveshare28v2_omote_sec2_receipt_compile
```

The compile probe is `src/receipt_mbedtls_compile_probe.cpp`.
It calls the actual C++ `verify()` and mbedTLS paths inside an
uncalled, exported function. It neither registers an endpoint nor
executes at boot. The ordinary OMOTE main source and experimental LAB
remain intact; there are **no NVS writes or eFuse changes** by the
new receipt code.

Expected: `SUCCESS` for
`waveshare28v2_omote_sec2_receipt_compile`. If the build fails,
record the first *actual compiler error*, not only the final summary.

### 2. Optional standalone physical serial diagnostic — FLASHES a different image

Before using this target, ensure the working OMOTE firmware can be
restored, the appropriate PlatformIO upload port is selected, and
the existing live Security2 LAB session is stopped. **Do not upload
this image merely to perform the build acceptance test.**

Build without touching the device:

```bash
~/.platformio/penv/bin/platformio run -e waveshare28v2_omote_sec2_receipt_device_lab
```

Only if deliberately conducting a separate physical test:

```bash
~/.platformio/penv/bin/platformio run -e waveshare28v2_omote_sec2_receipt_device_lab -t upload --upload-port /dev/ttyACM0
~/.platformio/penv/bin/platformio device monitor -p /dev/ttyACM0 -b 115200
```

This is a **standalone serial-only sketch**, not OMOTE. Flashing it
**temporarily replaces the running application image**. It does NOT
change NVS by design, but flashing always changes firmware flash.
The serial output should include:

```text
[RECEIPT-LAB] isolated firmware mbedTLS offline vector verification
[RECEIPT-LAB] ha valid=1 tamper_refused=1 session_refused=1
[RECEIPT-LAB] device valid=1 tamper_refused=1 session_refused=1
[RECEIPT-LAB] FINAL PASS; paired=0 deployment=0 ota=0 efuse=0
```

Any `valid=0`, `tamper_refused=0`, `session_refused=0` or
`FINAL FAIL` is a failure. Do not proceed to real enrollment.

## Host-only guard tests

```bash
python3 -m unittest discover -s tools/config-v2-tests -p 'test_receipt_espidf_build_guards.py' -v
python3 -m unittest discover -s tools/config-v2-tests -p 'test_enrollment_receipt_crosslang.py' -v
```

GitHub Actions workflow `.github/workflows/enrollment-receipt-offline.yml`
runs the host tests and requests both PlatformIO builds **without upload**.
A CI build failure needs investigation; neither target is considered
proven until a real ESP-IDF build succeeds.

## Security limits and remaining work

The fixed vectors contain synthetic transaction/session data. An ESP32
serial PASS would establish *mbedTLS compatibility and valid public
receipt verification*, not a live authenticated Security2 transcript.

Production pairing remains blocked until:
- canonical live Security2 transcript is independently derived and
  checked by HA and the ESP32;
- the physical touch confirmation and HA-admin approval are bound to
  the exact transaction;
- the two-phase durable journal is crash-tested on physical hardware;
- device private key protection and recovery/revocation policies are
  settled without unexpectedly changing irreversible eFuse settings;
- authorized reconnect and deployment gates are separately verified.

The stable firmware branch, existing LAB pairing-readiness behavior,
Home Assistant candidate identity, configuration deployment and OTA
have not been altered by this milestone.
