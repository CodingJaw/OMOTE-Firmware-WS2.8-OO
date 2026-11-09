# OMOTE Waveshare Security2 patch-1 lab — experimental IDF 5.5.5 migration

**Status (2026-11-09): new target reached a compile failure in inherited LovyanGFX 1.2.0 under ESP-IDF 5.5.5. An isolated dependency fix pins LovyanGFX 1.2.36; rebuild pending. No new-target firmware.bin/firmware.elf exists yet. Firmware has NOT been accepted on hardware. This is NOT production pairing.**

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

1. Flash the **new** opt-in target with the exact environment name above.
2. Confirm the Waveshare touchscreen and Settings page load normally.
3. Press Start lab and check the listener actually advertises patch 1.
4. On a fresh laboratory window, run:

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
