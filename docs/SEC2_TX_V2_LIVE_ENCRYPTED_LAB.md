# Security2 transaction v2 — opt-in live encrypted LAB

Updated: 2026-11-08. **Live encrypted v2 proof PASS on Waveshare hardware; new touchscreen linkage NOT YET tested.**
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

The earlier receipt vectors were offline-only. On 2026-11-08 the user also
reported a successful **live Security2 SRP6a/AES-GCM, mutual P-256 signing,
and matching v2 transaction SHA-256** on the actual Waveshare:

```text
PASS: Security2 SRP6a session and AES-GCM encrypted probe.
PASS: both P-256 signatures bind the same live Security2 lab transaction.
PASS: device and HA derived identical verified transcript SHA-256.
LAB ONLY SHA-256 transcript: 1be0fd4566fc8f4863e75ddc0fb083e59570c0bd9ac435f06676e9f249778541
NOT PAIRED: no HA trust, device trust, deployment or OTA.
```

The subsequent change connecting verified v2 proof evidence to a physical
touchscreen review is separate and **not yet hardware-verified**.

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

### Follow-up: verified v2 proof to touchscreen review (NEW, untested)

The v2 commit now returns the authenticated **public** HA and device keys
only after checking HA's P-256 signature, producing the device signature,
and deriving the matching transcript hash. The encrypted callback calculates
SHA-256 public fingerprints and forwards them, under a mutex, into the
existing volatile same-session `labReview_`. That state machine requires
both independently verified public keys to bind to exactly one protocomm
session ID. On success, the real LVGL touchscreen displays the HA SHA-256
public fingerprint and allows a **local-only** Review OK or Reject gesture.
The network callback cannot acknowledge review; it can only supply signed
proof evidence. No persistence or configuration privileges result.

The manual Python client now prints the verified HA and device public
fingerprints after successful v2 proof verification. Compare the complete
HA fingerprint on the terminal to the four rows on the touchscreen, then
press Review OK if they match, otherwise Reject. The touchscreen response
is not yet returned to HA and does not authorize durable enrollment.


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

These are build/host tests only and do not touch flash. The new
v2-to-touchscreen linkage must pass a fresh PlatformIO build before upload.
The prior live v2 hardware PASS does **not** validate this new linkage.
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

The user already confirmed this live v2 cryptographic result on
2026-11-08. The next physical test must additionally confirm that the
HA fingerprint printed by the Python client appears as four lines on the
Waveshare after the v2 COMMIT, and that only an actual local touch on
**Review OK** or **Reject** dismisses that review state.

A successful terminal printout alone does not prove the touchscreen review
has worked. Report whether the screen displays the expected fingerprint,
whether Review OK is visible, and what appears after physically tapping it.

## Optional encrypted local touchscreen review status (new, UNVERIFIED)

A third opt-in v2 endpoint, `omote-tx2-review`, accepts a 33-byte encrypted
payload: `0x02 || verified_transcript_SHA256[32]`. It responds with
`0x02 || state || same_digest[32]` where `state=0` means waiting for
the physical Review OK button and `state=1` means the touchscreen
acknowledged local evidence. Rejected, expired, wrong-session and
wrong-digest requests fail closed. The returned status is NOT paired,
NOT authorized, and DOES NOT bind an HA administrator.

The endpoint requires the authenticated callback Security2 session ID to
equal the session that completed the mutual v2 proof, the supplied digest
to equal the device's volatile verified transcript, and the touchscreen
review to belong to exactly the same session. It only reads state: there
is **no network approval action or persistent trust write**.

The matching contract is
`Platformio/tools/config-v2-tests/sec2_tx_v2_local_review_lab_wire_contract.json`;
a byte-equivalent contract is mirrored as a HACS fixture and checked in CI.
The default manual `--tx-v2-lab` mode remains unchanged.

To test the new opt-in poll after building and uploading the
`waveshare28v2_omote_sec2_tx_v2_lab` firmware:

```bash
export IDF_PATH="$HOME/Documents/026code/esp-idf"
cd ~/Documents/026code/OMOTE-Firmware-WS2.8-OO/Platformio
python3 -m unittest discover -s tools/config-v2-tests \
  -p 'test_sec2_tx_v2_lab_client.py' -v
python3 -m unittest discover -s tools/config-v2-tests \
  -p 'test_sec2_tx_v2_runtime_guards.py' -v
python3 -m unittest discover -s tools/config-v2-tests \
  -p 'test_sec2_lab_boundary.py' -v
~/.platformio/penv/bin/platformio run -e waveshare28v2_omote_sec2_tx_v2_lab
# If all tests and the build pass, upload as described above.
# On physical remote: Settings > SEC2 encrypted LAB test > Start lab
python3 tools/config-v2-tests/sec2_lab_client.py \
  --host 10.0.10.121 --tx-v2-lab --tx-v2-await-touch --ack-lab-only
```

Compare the full HA SHA-256 public fingerprint displayed in the terminal
with the four rows on the Waveshare, then physically tap Review OK.
The client polls the same completed SRP6a/AES-GCM session for up to 45 seconds.
Expected only after a verified local press:
`PASS: matching live v2 transcript acknowledged by local touchscreen.`
If the touchscreen is rejected, expires, or the connection closes, the
client fails without reporting acceptance. No code or status from this
endpoint may be used as a production trust grant.

This addition has been committed but NOT YET built or accepted on hardware.
The previous live crypto PASS does not prove local touchscreen status polling.

## One-shot signed physical-touch receipt LAB (NEW — unverified)

Following the successful real-device physical Review OK test on 2026-11-08,
a separate optional encrypted endpoint `omote-tx2-touch-receipt` has been
added to the same **opt-in** laboratory firmware target. It is not a
production enrollment receipt and does not change the existing v2 proof or
review-poll commands.

After the HA client verifies both v2 P-256 signatures, their transcript
hash, the matching HA fingerprint on the touchscreen and encrypted status
`Reviewed`, the client sends one more encrypted request:

- Request: `0x02 || verified_transcript_SHA256[32] || fresh_random_challenge[32]`
- Reply: `0x02 || 0x01_local_reviewed || u8(DER_signature_length) || P256_DER_signature`
- Signed content: ASCII `OMOTE-SEC2-TX2-TOUCH-RECEIPT-LAB-V1\\n`
  then `u8(2) || u32_be(Security2_callback_session) || transcript_SHA256[32] || challenge[32] || u8(1)`.

The remote rechecks the session ID and transcript against its volatile
completed v2 transaction, requires the Review state to have been set by
physical touch on this session, and signs with the LAB NVS-backed P-256
software device key **at most once**. A different session, transcript,
missing physical review, all-zero challenge, repeated request, expired
window or rejected review fails closed.

The Python client verifies the signature against the **device public key
from its independently verified v2 transaction**, not an untrusted network
identifier. The challenge is random and unique for this request.
Any malformed, forged, stale or mismatched response is rejected. The
signed receipt is never persisted as HA trust and never enables privileged
operations.

**New acceptance tests — before flashing:** from the PlatformIO directory,
update the feature branch, then run:

```bash
python3 -m unittest discover -s tools/config-v2-tests \
  -p 'test_sec2_tx_v2_lab_client.py' -v
python3 -m unittest discover -s tools/config-v2-tests \
  -p 'test_sec2_tx_v2_runtime_guards.py' -v
python3 -m unittest discover -s tools/config-v2-tests \
  -p 'test_sec2_lab_boundary.py' -v
~/.platformio/penv/bin/platformio run -e waveshare28v2_omote_sec2_tx_v2_lab
```

After all three test suites and the PlatformIO build pass, flash only the
opt-in lab firmware. On the remote physically tap Start lab; then execute:

```bash
export IDF_PATH="$HOME/Documents/026code/esp-idf"
python3 tools/config-v2-tests/sec2_lab_client.py \
  --host 10.0.10.121 --tx-v2-lab --tx-v2-await-touch \
  --tx-v2-attest-touch --ack-lab-only
```

After comparing the full HA public fingerprint, physically tap Review OK.
The client should additionally print:

```text
PASS: device P-256 signature confirms same-session physical LAB review.
LAB ONLY: signed touch receipt is one-shot and NOT pairing.
```

This new receipt has **not been built or exercised on Waveshare hardware**
yet. The previous successful Review OK poll does NOT prove this signature.
See `Platformio/tools/config-v2-tests/sec2_tx_v2_signed_touch_receipt_lab_contract.json`
and the mirrored read-only HACS fixture.

### Production-security blocker: legacy Security2 AES-GCM nonce

The official ESP-IDF 5.3.2 Python Security2 implementation currently
initializes AES-GCM with the device-provided nonce and reuses it for
multiple `encrypt_data`/`decrypt_data` calls. Such nonce reuse can
compromise AES-GCM confidentiality and integrity. This lab experiment
MUST NOT be represented as a production-secure transport until a
compatible, independently tested unique-nonce/counter scheme is
implemented on **both** ESP32 and Home Assistant. The separate P-256
signatures add transcript/proof integrity but do not make the legacy
AES-GCM implementation acceptable for production key exchange.

## Listener startup regression — fixed in feature branch, awaiting hardware

On 2026-11-08 the signed-receipt LAB firmware displayed
`Security2 lab listener failed. no trust or config changed.` immediately
after Start lab. Review of the ESP-IDF v5.3.2 HTTPD implementation and
the firmware registration sequence identified an endpoint-count overflow:
`HTTPD_DEFAULT_CONFIG().max_uri_handlers = 8`, while the signed-receipt
build had attempted **9** registrations (Security2 session + version +
probe + two original standalone P-256 proof endpoints + four v2 endpoints).
The last endpoint registration fails, and the listener properly closes.

The isolated `waveshare28v2_omote_sec2_tx_v2_lab` now omits the two
redundant legacy candidate/device proof endpoints, leaving **7** registered
handlers. The original `waveshare28v2_omote_sec2_lab` retains those
standalone test endpoints, with **5** total handlers. The version, SRP6a,
AES-GCM, persistent message quota, local-touch gate, and four v2 endpoints
are unchanged. PlatformIO's ESP-IDF installation does not need modification.

The standalone Python client now rejects mixing `--ha-proof` or
`--device-proof` with `--tx-v2-lab` before opening a connection. The
new `test_sec2_lab_endpoint_budget.py` regression test checks the compile
guards and URI limits. This patch was source-reviewed, **not yet compiled
or validated on physical hardware**.

Run from the PlatformIO directory:

```bash
python3 -m unittest discover -s tools/config-v2-tests -p 'test_sec2_lab_endpoint_budget.py' -v
python3 -m unittest discover -s tools/config-v2-tests -p 'test_sec2_lab_boundary.py' -v
python3 -m unittest discover -s tools/config-v2-tests -p 'test_sec2_tx_v2_runtime_guards.py' -v
python3 -m unittest discover -s tools/config-v2-tests -p 'test_sec2_tx_v2_lab_client.py' -v
~/.platformio/penv/bin/platformio run -e waveshare28v2_omote_sec2_tx_v2_lab
```

After they pass, upload the lab image and tap Start lab. The listener
should open normally without clearing NVS or changing any trust. Continue
with `--tx-v2-lab --tx-v2-await-touch --tx-v2-attest-touch --ack-lab-only`
only after confirming the touchscreen displays the new six-digit PIN.

## Still blocking production enrollment

- The software LAB device key is NOT hardware-protected and the
  current HA proof is ephemeral, not the persistent HA candidate identity.
- The new v2 proof-to-touchscreen LAB link still requires board build and
  hardware validation. Even if confirmed, that local acknowledgement is
  **not** an authorized HA administrator approval or production enrollment.
- HACS does not yet receive an authenticated, anti-replay review approval;
  it still needs durable per-device HA identity, revocation and re-pairing.
- There is no persistent trust/revocation journal, authenticated reconnect,
  configuration deployment, OTA or eFuse change.
- Power-loss replay protection and two-party durable reconciliation remain
  unfinished. A 30-second one-shot memory record is an experimental
  anti-replay guard, not a replacement for a production durable journal.

**All production privileges remain disabled.**
