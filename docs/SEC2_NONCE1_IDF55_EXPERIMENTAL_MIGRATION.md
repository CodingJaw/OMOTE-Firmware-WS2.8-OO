# OMOTE Waveshare Security2 patch-1 lab — experimental IDF 5.5.5 migration

**Status (2026-11-09): user reports all requested patch-1 laboratory tests PASS, following successful IDF 5.5.5 compilation and preflight. This supports progression to negative-case hardening; no raw runtime transcript was supplied in the latest reply. NOT PAIRED; no deployment or trust enabled.**

## November 11 upload-stage package resolution failure — NOT FLASHED

The user attempted `-t upload` for the isolated Security2 patch-1 build.
PlatformIO did not reach serial upload: it listed
`framework-espidf 5.5.5`, but NOT
`framework-arduinoespressif32`, then its Tasmota
`builder/frameworks/arduino.py` attempted `Path(FRAMEWORK_DIR)`
with `FRAMEWORK_DIR=None`. This is a **missing/unregistered
Arduino framework package**, not an application compilation error.

The pinned Tasmota platform 2026.10.50 declares
`engines.platformio >=6.2.0-0`. An older Core could contribute to
the package resolver failure, but the user's installed Core version
was not included in the upload log. Do NOT presume the exact cause
until checking version and registered packages.

Pull the firmware feature branch and run the added **read-only** guard:

```bash
cd ~/Documents/026code/OMOTE-Firmware-WS2.8-OO/Platformio
~/.platformio/penv/bin/platformio --version

python3 -m unittest discover -s tools/config-v2-tests \
  -p 'test_sec2_nonce1_upload_environment.py' -v
python3 tools/config-v2-tests/sec2_nonce1_upload_environment.py

~/.platformio/penv/bin/platformio pkg list \
  -e waveshare28v2_omote_sec2_tx_v2_nonce1_lab
```

The guard requires Core 6.2+, the correct Tasmota platform,
Arduino framework 3.3.12, and ESP-IDF framework 5.5.5. It does
not repair packages and never contacts the device. If any item
BLOCKS, do not attempt another upload yet.

**If Core is already 6.2+ but Arduino is absent**, reinstall the
exact framework declared in the existing experimental `platform_packages`
without saving changes to platformio.ini:

```bash
~/.platformio/penv/bin/platformio pkg install \
  -e waveshare28v2_omote_sec2_tx_v2_nonce1_lab \
  --tool "framework-arduinoespressif32 @ https://github.com/tasmota/arduino-esp32/releases/download/v3.3.12-1/framework-arduinoespressif32.tar.xz" \
  --no-save
```

Then rerun the read-only guard. If the package still does not
register, preserve `platformio pkg list` and the first error. Do
NOT remove `~/.platformio/packages` or `.pio` globally.

**If Core is below 6.2**, preserve the existing working toolchain
and create an **isolated** pioarduino CLI installation and cache,
instead of upgrading the existing stable environment in place:

```bash
python3 -m venv ~/Documents/026code/pio-nonce1-venv
~/Documents/026code/pio-nonce1-venv/bin/python -m pip install \
  'pioarduino==6.2.0.post1'

export PLATFORMIO_CORE_DIR="$HOME/.platformio-nonce1"
~/Documents/026code/pio-nonce1-venv/bin/platformio --version
~/Documents/026code/pio-nonce1-venv/bin/platformio pkg install \
  -e waveshare28v2_omote_sec2_tx_v2_nonce1_lab
```

This isolated cache requires independent framework/toolchain
downloads and a separate compile. Do not run the standard default
guard when using an isolated cache; supply both
`--platformio ~/Documents/026code/pio-nonce1-venv/bin/platformio`
and `--core-dir ~/.platformio-nonce1`. The existing working
2025.03.30 environment remains untouched.

After the selected environment reports all PASS, repeat a **build
without upload** first. Preserve a known-good firmware image and
USB recovery before attempting another upload. In particular, do
not use direct `esptool write_flash` to bypass the build system,
and do not erase NVS.

## Confirmed build acceptance — November 9, 2026

The physical user's Debian PlatformIO build completed `SUCCESS` for
`waveshare28v2_omote_sec2_tx_v2_nonce1_lab` with Tasmota
2026.10.50, Arduino 3.3.12-1, ESP-IDF 5.5.5, and LovyanGFX 1.2.36.
The user ran the hardware preflight using the separate
`$HOME/Documents/026code/esp-idf-nonce1` checkout, which returned:

```text
PASS: isolated pinned PlatformIO target
PASS: patched Python ESP-IDF checkout: Python counter-capable; IDF source counter-capable
PASS: new-target firmware.bin
PASS: new-target firmware.elf
READY FOR LAB HARDWARE EVALUATION ONLY; NOT PAIRED
```

The exact local artifacts reported were:

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| `firmware.bin` | 2,384,432 bytes | `9d46bf81b127dc5fea2997b8dc87a1ce33434da131f8256fdaeab6462f2c2785` |
| `firmware.elf` | 62,899,472 bytes | `ebece63cb4274f7f2c759044d5cfe2cd0eff06ef8a2d99cdc4636f639267de1d` |

The build emitted linker warnings related to `statvfs` and a
`.note.GNU-stack` section, but compiled and linked successfully.
This does **NOT** demonstrate that the image was actually flashed,
booted, negotiated Security2 patch 1 or passed physical review.

## Why this target exists

On physical Waveshare hardware, the existing `waveshare28v2_omote_sec2_tx_v2_lab`
successfully completed SRP6a, legacy AES-GCM transport, mutual signed P-256
transaction, physical Review OK, and a one-time device-signed touch receipt.
That baseline remains unchanged and must remain available as a fallback.

Both original development installations reported the legacy/fixed-nonce
Security2 implementation:

- `~/Documents/026code/esp-idf` — Python provisioning files (separate from firmware).
- `~/.platformio/packages/framework-espidf` — firmware framework used for the actual build.

ESP-IDF 5.3.2 uses the same AES-GCM nonce for repeated encrypted messages
within a Security2 session. This is unacceptable for future private
credentials, configuration deployment or persistent pairing. Official
ESP-IDF 5.4.2/5.5.5 introduce Security2 **patch 1**, where a 12-byte nonce
consists of 8 session-random bytes plus a 32-bit network-order counter.
Sequential encrypt/decrypt operations advance the counter.

## Exact, matched experimental toolchain

`[env:waveshare28v2_omote_sec2_tx_v2_nonce1_lab]` inherits **all**
current opt-in Security2 v2 lab flags, but overrides only these packages:

| Component | Pinned candidate | Source |
| --- | --- | --- |
| Tasmota PlatformIO platform | 2026.10.50 | https://github.com/tasmota/platform-espressif32/releases/tag/2026.10.50 |
| Tasmota Arduino | v3.3.12-1 | https://github.com/tasmota/arduino-esp32/releases/tag/v3.3.12-1 |
| ESP-IDF (pulled by platform) | 5.5.5 | https://github.com/tasmota/esp-idf/releases/tag/v5.5.5 |
| Security2 | patch 1 required | `protocomm_security2.patch_ver` |

The new flag is `OMOTE_CONFIG_V2_SEC2_NONCE1_LAB=1`. Its listener
checks the **actually linked** Security2 object before material generation,
LAB key preparation, HTTPD startup or version advertisement. If that object
does not advertise patch 1, listener startup is refused. This is a hard,
fail-closed check, not a request to silently upgrade an old firmware image.

**Caution:** The newer Arduino/ESP-IDF combination changes major toolchain
and runtime APIs. It may fail to compile existing OMOTE dependencies, touch
drivers, or LVGL code. Do not flash a new image unless the full build and
local tests pass and the previous working firmware is available to restore.
PlatformIO will download a new platform, Arduino framework and IDF packages;
do not cancel the working toolchain or overwrite the existing IDF checkout.

## November 9 compiler regression — LovyanGFX GPIO HAL

The first attempted new-toolchain build FAILED while compiling inherited
`LovyanGFX@1.2.0`:

```text
src/lgfx/v1/platforms/esp32s3/Bus_RGB.cpp:98:
error: 'gpio_hal_iomux_func_sel' was not declared in this scope
```

LovyanGFX upstream fixed the ESP-IDF 5.5 GPIO API change (see
https://github.com/lovyan03/LovyanGFX/discussions/751 and
https://github.com/lovyan03/LovyanGFX/issues/734).
The exact released **LovyanGFX 1.2.36** source contains an
`ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)` compatibility branch
which calls `gpio_hal_func_sel`. Only the opt-in
`waveshare28v2_omote_sec2_tx_v2_nonce1_lab` has a copied dependency
list with the single version changed to `lovyan03/LovyanGFX@1.2.36`.
All established environments retain the original `1.2.0` dependency.

The new `test_sec2_nonce1_build_isolation.py` asserts that the
experimental dependency list equals the inherited stable list except
for this single version. The offline hardware preflight rejects an
experimental environment still pinned to `1.2.0`.

Rebuild the **experimental** target after pulling the feature branch;
do not modify files under `.pio/libdeps` manually, erase flash/NVS,
or flash until the compile and preflight both pass. If the compiler
reports another incompatibility, fix that specifically in the opt-in
target instead of making broad stable-library updates.

## Stage 1 — offline tests and compile, NO FLASH

From a shell on your Debian development machine:

```bash
cd ~/Documents/026code/OMOTE-Firmware-WS2.8-OO
git pull --ff-only origin feature/config-v2-transactional
cd Platformio

python3 -m unittest discover -s tools/config-v2-tests \
  -p 'test_sec2_nonce1_build_isolation.py' -v
python3 -m unittest discover -s tools/config-v2-tests \
  -p 'test_sec2_nonce1_wire_model.py' -v
python3 -m unittest discover -s tools/config-v2-tests \
  -p 'test_sec2_nonce_policy.py' -v

~/.platformio/penv/bin/platformio run \
  -e waveshare28v2_omote_sec2_tx_v2_nonce1_lab
```

Do not upload this unverified build. Report the first compiler error, if any,
before modifying the stable/working dependencies. The PlatformIO package
download can be substantial because IDF, Arduino and toolchains are separate.

## Stage 2 — separate sparse IDF checkout for Python provisioning

**Never** run a recursive submodule update for this Python experiment. In
particular, the large `components/esp_wifi/lib` submodule is not needed.
Keep the old `~/Documents/026code/esp-idf` checkout unchanged.

```bash
git clone --depth 1 --filter=blob:none --sparse \
  --single-branch --branch v5.5.5 \
  https://github.com/espressif/esp-idf.git \
  ~/Documents/026code/esp-idf-nonce1

git -C ~/Documents/026code/esp-idf-nonce1 sparse-checkout set \
  tools/esp_prov \
  components/protocomm/python \
  components/protocomm/src/security \
  components/wifi_provisioning/python
```

Run these only once if that destination does not already exist. These are
ordinary Git commands, not a firmware build, and do not modify the old
ESP-IDF checkout or install ESP-IDF submodules.

If `~/Documents/026code/esp-idf-nonce1` already exists, do not clone
over it. Instead verify `git -C ~/Documents/026code/esp-idf-nonce1
describe --always --tags` and the sparse directories before using it.

```bash
export IDF_PATH="$HOME/Documents/026code/esp-idf-nonce1"
cd ~/Documents/026code/OMOTE-Firmware-WS2.8-OO/Platformio
python3 tools/config-v2-tests/sec2_lab_client.py --check-prereqs
python3 tools/config-v2-tests/sec2_lab_client.py --check-sec2-nonce
python3 tools/config-v2-tests/audit_sec2_platformio_framework.py
```

The first check should confirm Python dependencies, the second should find
counter-capable source on **both** sides of this IDF checkout. The
PlatformIO audit may find multiple installed firmware packages: inspect
which package was actually selected by the new build before attributing a
counter capability to any flashed image. A counter-capable checkout alone
is not proof of correct authentication or runtime behavior.

### Hardware acceptance preflight (new)

After the new-target build has completed, run the following **read-only**
preflight from the `Platformio/` directory:

```bash
python3 -m unittest discover -s tools/config-v2-tests \
  -p 'test_sec2_nonce1_hardware_preflight.py' -v
python3 tools/config-v2-tests/sec2_nonce1_hardware_preflight.py
```

The preflight verifies that the **new** Python `IDF_PATH` source checkout
contains counter-capable Security2, the selected PlatformIO environment has
the exact matched experimental toolchain pins, and the new target's
`firmware.bin` and `firmware.elf` are present and nontrivial. It prints
SHA-256 hashes of both artifacts for comparison/archival. It never
connects to the remote, flashes firmware, checks a PIN, or modifies NVS.

`READY FOR LAB HARDWARE EVALUATION ONLY; NOT PAIRED` means that these
**offline** prerequisites passed, not that the binary's provenance or
the actual physical device version has been proved. Missing old-source
paths or missing new-target firmware artifacts correctly BLOCK the test.

Before any experimental upload, keep a retrievable copy of the prior
working `waveshare28v2_omote_sec2_tx_v2_lab` firmware image and verify
that USB serial recovery is available on `/dev/ttyACM0`. Do NOT erase
flash or NVS. A development build and a screenshot of the compile PASS are
not substitutes for a recovery image.

## Stage 3 — conditional hardware test (requires Stage 2 preflight PASS)

Only after a successful Stage 1 build, Stage 2 `READY FOR LAB
HARDWARE EVALUATION ONLY` preflight, explicit USB recovery readiness,
and review of changed runtime dependencies:

1. Before flashing, preserve the prior known-good image and ensure USB
   serial recovery is available; an experimental firmware upload replaces
   the working application. Do not erase NVS or program any eFuses.
   After all prerequisites, flash the **new** opt-in target using:

```bash
~/.platformio/penv/bin/platformio run \
  -e waveshare28v2_omote_sec2_tx_v2_nonce1_lab \
  -t upload --upload-port /dev/ttyACM0
```

   If the new firmware fails to boot, restore the original laboratory
   target from its existing source and PlatformIO environment:

```bash
~/.platformio/penv/bin/platformio run \
  -e waveshare28v2_omote_sec2_tx_v2_lab \
  -t upload --upload-port /dev/ttyACM0
```

   These commands do not request an NVS erase. Keeping a copy of the
   previous working image is preferable to relying only on a rebuild.
2. Confirm the Waveshare touchscreen and Settings page load normally.
3. Press Start lab and run the **read-only unauthenticated** version
   check first. It never sends a PIN or SRP frame:

```bash
export IDF_PATH="$HOME/Documents/026code/esp-idf-nonce1"
python3 tools/config-v2-tests/sec2_nonce1_remote_check.py \
  --host 10.0.10.121 --ack-lab-only
```

   Expected: `PASS: remote LAB version endpoint advertises Security2 patch 1.`
   This is an **unauthenticated** version advertisement, not device
   identity authentication or proof of a flashed image.
4. If the version check passes and the lab window is still open, run
   the strict, encrypted and physically approved receipt test:

```bash
export IDF_PATH="$HOME/Documents/026code/esp-idf-nonce1"
cd ~/Documents/026code/OMOTE-Firmware-WS2.8-OO/Platformio
python3 tools/config-v2-tests/sec2_lab_client.py \
  --host 10.0.10.121 --require-sec2-patch1 \
  --tx-v2-lab --tx-v2-await-touch --tx-v2-attest-touch \
  --ack-lab-only
```

Do not enter the six-digit code in command history, shared files, or CLI
arguments. The interactive prompt remains protected; the Security2 lab
persistent five-message quota and explicit physical rearm remain enabled.

The full run must pass SRP6a/AES-GCM, mutual signatures, exact transcript,
physical Review OK and one-shot device-signed receipt. Then test
expiry/rearm/reconnect and error paths. Passing that diagnostic still
**does not** create a trusted HA pairing.

## Phase 4 — negative-case hardening (new, OFFLINE FIRST)

The user's 2026-11-09 follow-up reports the requested positive patch-1
laboratory tests passed. No fresh terminal transcript was attached to
that reply; treat the full details of live encryption and receipt
verification as **user-reported** rather than independently captured
in this revision. Existing working listener and signed-receipt wire
framing are intentionally unchanged.

Before durable HA credentials or config deployment, test all negative
boundaries with the new firmware-owned, HACS-mirrored
`sec2_nonce1_negative_acceptance_contract.json`.

### Offline host tests (no hardware; safe to run repeatedly)

```bash
cd ~/Documents/026code/OMOTE-Firmware-WS2.8-OO/Platformio

bash tools/config-v2-tests/run-host-tests.sh

python3 -m unittest discover -s tools/config-v2-tests \
  -p 'test_sec2_nonce1_wire_model.py' -v

python3 -m unittest discover -s tools/config-v2-tests \
  -p 'test_sec2_nonce1_negative_acceptance.py' -v

python3 -m unittest discover -s tools/config-v2-tests \
  -p 'test_sec2_tx_v2_lab_client.py' -v
```

The new C++ `sec2_lab_review_lifecycle_test.cpp` checks exact 60-second
touch expiry, exact 120-second listener expiry even **after** Review OK,
physical rejection, close and rearm, and millis rollover. It is built
and executed by the existing host test runner. The extended Python
nonce model verifies fresh keying/IV on reconnect, stale ciphertext
rejection across sessions, failed-tag terminal behavior, lost-response
desynchronization, and correct new-session recovery. These tests
simulate cryptography and state transitions; they do not prove actual
IDF runtime concurrency or a real reconnect.

### Optional physical negative checks (manual, quota-consuming when using SRP)

Perform only when the working lab firmware is backed up and USB serial
recovery is available. Do not automatically run malformed network
traffic or repeated incorrect PIN trials: each Security2 frame is
counted persistently. The lab can be manually rearmed on the physical
remote, not over the network.

1. **Physical reject:** start a fresh encrypted LAB window; run the
   existing strict `--require-sec2-patch1 --tx-v2-lab
   --tx-v2-await-touch --tx-v2-attest-touch --ack-lab-only` test.
   After the verified fingerprints appear, press **Reject** instead of
   Review OK. The Python client must fail during review polling and
   must NOT print the device-signed touch-receipt PASS.
2. **Expired listener without SRP:** start a lab window, allow it to
   expire without entering a code, then run the read-only
   `sec2_nonce1_remote_check.py --host 10.0.10.121 --ack-lab-only`.
   The listener must be unavailable. This does not consume an SRP
   attempt, but the app must continue operating.
3. **Reopen:** physically rearm/start another lab and run the strict
   positive test, verifying a fresh ephemeral HA fingerprint and
   fresh session transcript. The device's existing software-key
   fingerprint may remain the same; it is NOT a production identity
   or trust assertion.

These hardware negative checks are **not yet independently verified**
in this phase. A successful positive lab cannot substitute for the
reject/expiration, reconnect and downgrade tests.

### Hardening gate for production

The remote `proto-ver` Security2 patch advertisement is not
authenticated. Strict client patch-1 checking prevents a *simple*
legacy fallback, but does not establish downgrade-resistant
production negotiation. Authenticating the actual chosen cipher suite
and patch inside both signed parties' transcript remains a separate
requirement. Session counters and real HTTPD callback concurrency
also require live verification before sending durable credentials.

## Outstanding production requirements

- Strict no-downgrade authentication of the selected Security2 patch.
- Correct nonce uniqueness under concurrent requests, retries and reconnects.
- Proper nonce counter exhaustion handling and distinct session keys.
- Durable HA administrator approval bound to the exact physical device review.
- Hardware-protected or otherwise threat-model-appropriate, revocable device keys.
- Rollback-safe trust commit, unpair/re-pair, backup and recovery.
- Authenticated configuration read/write deployment and OTA with separate consent.

**Current authorization:** NOT PAIRED, no durable trust, no remote writes,
no deployments, no OTA and no eFuse programming.
