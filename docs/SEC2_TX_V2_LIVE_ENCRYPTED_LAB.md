# Security2 transaction v2 — opt-in live encrypted LAB

Date: 2026-11-07. **Development / unverified hardware build.**
Branch: `feature/config-v2-transactional` only.
Normal OMOTE, previously accepted `waveshare28v2_omote_sec2_lab`,
the standalone receipt test and the stable branch are not modified to
enable this protocol.

## Verified incoming milestone

The user reported that ALL earlier offline v2 proof tests passed.
The HACS offline tests and firmware Python/C++ cross-language tests passed,
and the Waveshare ESP32-S3 already printed:

```text
[RECEIPT-LAB] ha valid=1 tamper_refused=1 session_refused=1
[RECEIPT-LAB] device valid=1 tamper_refused=1 session_refused=1
[RECEIPT-LAB] FINAL PASS; paired=0 deployment=0 ota=0 efuse=0
```

This confirms only signed fixed receipt vectors on-device. The present
live v2 endpoint integration is **new and not yet built/tested on hardware**.

## Isolated build target and touch-gate

`waveshare28v2_omote_sec2_tx_v2_lab` extends the existing
`waveshare28v2_omote_sec2_lab` image and adds ONLY the
`OMOTE_CONFIG_V2_SEC2_TX_V2_LAB` compile flag. Existing
Security2 SRP6a/AES-GCM, a 120-second lab window, six-digit
touchscreen PIN, physical Start lab UI, and persistent anti-bruteforce
SRP-frame budget remain in place. The normal Waveshare UI runs;
the standalone receipt diagnostic's blank-screen behavior does not apply.

Two additional encrypted endpoints are registered ONLY in this target:
`omote-tx2-open` and `omote-tx2-commit`, on port 18082.
The normal public port-80 API does not register them. See
`tools/config-v2-tests/sec2_tx_v2_live_lab_wire_contract.json`.

### Encrypted OPEN

Request: `u8(2) || HA ephemeral P256 SEC1 public[65] || HA random[32]`.
Reply: `u8(2) || locally authenticated Security2 session_id(u32 BE) ||
epoch(u32 BE, LAB=1) || remote-generated tx_id[16] ||
device_id_len(u8=8) || device_id[8] || existing LAB device public[65] ||
remote-generated device challenge[32]`.

The LAB device ID is the first eight bytes of SHA256 of the LAB
device public key; it is NOT a production device attestation or immutable
hardware identity. The remote generates the transaction ID/challenge;
both parties keep a single volatile, 30-second pending transaction bound
to exactly the encrypted session ID. No network caller can supply the
device-generated transaction ID or challenge.

### Encrypted COMMIT

HA signs `OMOTE-SEC2-TX-PROOF-HA-V2\n || canonical_context`, including
session, tx ID, device ID, epoch, both public keys and challenges.
Request: `u8(2) || u8(HA_DER_length) || HA_DER_signature`.

The ESP32 validates this signature BEFORE signing its own locally
constructed context with its existing NVS-backed LAB software key.
Device signature domain:
`OMOTE-SEC2-TX-PROOF-DEVICE-V2\n || canonical_context`.
Reply: `u8(2) || u8(device_DER_length) || device_DER_signature ||
SHA256(domain-separated canonical signed transcript)[32]`.

HA independently verifies both signatures, reconstructs the same
canonical context from the decrypted reply/current session cookie, and
checks the remote-supplied digest. No HA candidate identity is stored
by this manual test; the client uses a fresh ephemeral P256 key per run.
The device uses its existing LAB software P256 identity; as previously
documented, the existing Security2 LAB can create that software key
in ordinary NVS on the FIRST physically initiated lab session.
The new v2 coordinator itself writes no device trust record.

## Test order — DO NOT FLASH BEFORE BUILD PASSES

From `OMOTE-Firmware-WS2.8-OO/Platformio`:

```bash
git pull --ff-only origin feature/config-v2-transactional
python3 -m unittest discover -s tools/config-v2-tests \
  -p 'test_sec2_tx_v2_lab_client.py' -v
python3 -m unittest discover -s tools/config-v2-tests \
  -p 'test_sec2_tx_v2_runtime_guards.py' -v
python3 -m unittest discover -s tools/config-v2-tests \
  -p 'test_sec2_lab_boundary.py' -v
~/.platformio/penv/bin/platformio run -e waveshare28v2_omote_sec2_tx_v2_lab
```

These are build/host tests only and do not touch flash. The newly
extended code is **not yet independently validated by a PlatformIO build**.
Report first compilation error and do not upload if any tests/build fail.

After explicitly deciding to replace the standalone receipt diagnostic
with the full OMOTE lab UI (and having a tested restore image), the
separate physical test, only if the preceding build has passed:

```bash
~/.platformio/penv/bin/platformio run \
  -e waveshare28v2_omote_sec2_tx_v2_lab \
  -t upload --upload-port /dev/ttyACM0
python3 tools/config-v2-tests/sec2_lab_client.py --check-prereqs
# On remote: Settings > SEC2 encrypted LAB test > Start lab
# Read six-digit code from remote, enter it only at the secure prompt.
python3 tools/config-v2-tests/sec2_lab_client.py \
  --host YOUR_DEVICE_IPV4 --tx-v2-lab --ack-lab-only
```

Do NOT enter the six-digit PIN on the command line or in any shared
logs. `IDF_PATH` still needs to point to a compatible Espressif
source checkout for the official Security2 client. The SRP frame budget
is persisted, so repeated errors may require the existing on-device,
physically confirmed lab rearm process.

Expected manual outcome *only after real test verification*:
`PASS: both P-256 signatures bind the same live Security2 lab transaction`
and `PASS: device and HA derived identical verified transcript SHA-256`,
with `NOT PAIRED` explicitly printed. A build is not proof of a live
session. No test has yet reported this real v2 result.

## Still blocking production enrollment

- The software LAB device key is NOT hardware-protected and the
  current HA proof is ephemeral, not the persistent HA candidate identity.
- This proof does not bind an authorized HA admin or touchscreen Review OK
  to this exact signed transaction. The existing v1 LAB touch review is
  independent and must NOT be treated as approval for v2 enrollment.
- There is no persistent trust/revocation journal, authenticated reconnect,
  configuration deployment, OTA or eFuse change.
- Power-loss replay protection and two-party durable reconciliation remain
  unfinished. A 30-second one-shot memory record is an experimental
  anti-replay guard, not a replacement for a production durable journal.

**All production privileges remain disabled.**
