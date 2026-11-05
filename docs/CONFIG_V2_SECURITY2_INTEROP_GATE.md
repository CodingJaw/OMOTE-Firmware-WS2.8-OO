# Security2 interoperability gate — pairing candidate (NOT ENABLED)

Status: **read-only protocol discovery + isolated SRP material generation only**, November 4, 2026. This document does not authorize network pairing, unauthenticated provisioning, secret access, configuration writes, or firmware deployment.

## Candidate

- **Device:** ESP-IDF protocomm Security2 (SRP6a + AES-256-GCM) over
  bounded **Wi-Fi HTTPD** transport. No BLE pairing or security0 fallback.
- **Client:** evaluate the official ESP-IDF Python `tools/esp_prov/security/security2.py`
  and its protobuf/SRP/AES-GCM dependencies against HA Core 2024.9.1's
  supported Python runtime. A command-line demo is not a supported Home
  Assistant library; packaging and license compliance require review.
- **Local password:** fresh six-digit PIN generated with the existing
  `config_v2_pairing_policy` CSPRNG and shown ONLY on the Waveshare screen.
  Test whether `esp_srp_gen_salt_verifier` in the installed
  `protocomm/crypto/srp6a/esp_srp.h` can generate a NEW ephemeral salt and
  verifier from that PIN **on device** for each short pairing window.
  The API is documented by Espressif; target-toolchain availability, heap
  pressure, lifetime, and memory wiping are still unverified.
- **Identity:** PAKE proves possession of the displayed code, not the
  permanent device identity or the HA peer public key. Require a separate
  possession proof of the new HA public/private key, a device key pinned
  by HA, binding to the encrypted session, and physical touchscreen
  approval of the exact HA fingerprint before storing trust.
- **State:** existing persistent attempt reservation must be performed
  before a PIN candidate can be verified; max five guesses, reboot-resistant
  lockout, expiry and concurrency must be tested at the network boundary.
- **Next steps:** compile/link prototype with the exact Waveshare toolchain,
  inspect `sdkconfig` and IDF patch level, run a memory/CPU benchmark,
  integrate an isolated loopback Security2 handshake with the pinned
  Espressif Python client, and only then consider a device-local pairing
  coordinator. No endpoint may submit an unverified `VerifiedPeer`.

## Implemented preparation (2026-11-04)

- Firmware feature branch has a strictly **read-only** public
  `GET /api/config/v2/pairing/status` endpoint. Its entire response is pinned
  in `Platformio/tools/config-v2-tests/pairing_v1_preview_contract.json`.
  The response always reports disabled pairing, unavailable cryptographic
  session, no transport port and no deployment authorization.
- New `WaveshareOmote/config_v2_sec2_srp_material.*` uses the **real**
  ESP-IDF `esp_srp_gen_salt_verifier` API to produce a new 16-byte salt
  and verifier from a six-digit code supplied by the local, CSPRNG-backed
  pairing window. The owning object rejects non-six-digit PINs and wipes and
  frees its material on destruction. No fixed PIN is embedded.
- The SRP material is **not yet wired to a Security2 session**. It must remain
  valid throughout a future provisional pairing window and be destroyed
  on expiry/cancellation/approval. No network handler can request its PIN.
- The existing Arduino WebServer on port 80 stays legacy read-only.
  A separate bounded, short-lived Espressif HTTPD transport may be needed
  for Security2; transport ownership, port allocation and memory usage
  must be tested before any listener is started.
- HACS `main` version `0.1.0-dev20` adds
  `OMOTE Manager → Overview → Check pairing readiness`. It displays the
  preview or a legacy 404 as unavailable, never as paired. Its CI pins
  and compares this firmware-owned pairing contract.
- The static firmware API tests permit only the exact three public Config v2
  GET routes (capabilities, storage status, pairing status); they block
  pairing or transaction mutation routes.

**Remaining engineering gates before a live pair:** independent test of
SRP salt/verifier against Espressif's official Python Security2 client,
bounded encrypted HTTPD handshake and memory measurements, paired long-lived
device identity proof, HA key possession proof, persistent attempt lockout,
fingerprint confirmation on the actual touchscreen, HA secure credential
storage and revocation, reconnect and re-pairing. SHA-256 fingerprints or
mDNS identifiers alone are *not* proof of identity.

## Isolated encrypted interoperability probe (not paired; 2026-11-04)

The firmware feature branch now contains `config_v2_sec2_lab_transport.*`.
It uses Espressif's **real protocomm Security2 (SRP6a/AES-256-GCM)**
with a short-lived, *separate* HTTPD service on TCP **18082**. It exposes
only the official `prov-session` handshake endpoint, a harmless
`proto-ver` scheme/patch-version endpoint, and one encrypted
`omote-probe` endpoint exchanging the fixed strings
`OMOTE-SEC2-PROBE-V1` and `OMOTE-SEC2-OK-V1`.

**This is a laboratory interoperability target, not working
product pairing.** The listener is disabled in the ordinary Waveshare
firmware build and requires the separate `waveshare28v2_omote_sec2_lab`
environment. It never starts automatically or from network input. Only
in that opt-in LAB build, go to Settings → SEC2 encrypted LAB test and
physically tap **Start lab**. This calls `Probe::startFromTrustedLocalTouch()`
after reserving a persistent NVS attempt-window counter in the LAB-ONLY
`omsec2lab/windows` namespace (not production `omotepair/attempts`),
shows the local six-digit code and opens port 18082 for up to two minutes. Closing or
cancelling the local page also stops the listener. Normal port 80 remains
unchanged and read-only.

The LAB build now reserves a second flash-backed counter before **every
Security2 session/proof message**. The independent
`omsec2lab/secmsgs` key allows at most five Security2 frames across
reboots, so a normal two-message handshake consumes two reservations.
Failure to read, persist or verify the counter aborts the SRP request
**before** invoking Espressif's security handler. This is deliberately
more restrictive than five full PIN guesses and cannot be reset remotely.
The LAB code has no automated rearm mechanism yet; exhausting the quota
requires a separately reviewed physical recovery path before further tests.

No device identity, HA key-possession proof, peer-fingerprint approval,
credential persistence or remote-write endpoint is implemented in this
laboratory transport. The per-message limiter is necessary but **not
sufficient** for production protection. **Do not flash the lab image as a
production remote.**

The manual reference client at
`Platformio/tools/config-v2-tests/sec2_lab_client.py` calls Espressif's
official Python `Security2` and `Transport_HTTP`. It reads a PIN using
`getpass` (never CLI arguments), negotiates `prov-session`, sends a
cryptographically encrypted test request and verifies the decrypted
response. It cannot run against the standard firmware because the listener
is disabled; only the deliberately flashed lab image has the local action.
After opening a lab window via physical touch, the invocation is:

```sh
export IDF_PATH=/path/to/compatible/esp-idf
python3 Platformio/tools/config-v2-tests/sec2_lab_client.py \
  --host 10.0.10.121 --port 18082 --ack-lab-only
```

Only run this after the dedicated lab image passes CI, a verified
full-flash USB backup, and an explicit local Settings touchscreen action.
Two separate LAB NVS counters limit physical window openings and individual
Security2 messages. A successful handshake consumes two security-message
reservations. After five messages, new handshakes fail closed; there is no
automatic reset or production credential enrollment. This LAB transport
is not suitable for production pairing or Internet-exposed networks.
The user-selected final protocol also requires a stable device identity
and an HA public-key possession challenge bound to the encrypted session,
then a separate explicit on-device touchscreen confirmation of that peer.
It is not yet safe to expose a "Pair" button in HACS.

## Encrypted HA candidate-key proof (2026-11-04)

The next layer on the isolated lab build is an **encrypted, unprivileged
candidate HA public-key-possession proof**:

- After the real Security2 `prov-session` handshake, the optional reference
  client creates an **ephemeral** P-256 keypair and cryptographically signs
  `"OMOTE-SEC2-LAB-HA-PROOF-V1" || Security2_session_id_u32_BE ||
  public_key_SEC1_65 || fresh_client_nonce_32` using ECDSA/SHA-256.
  The private key never leaves the Python process and is not persisted.
- The payload is delivered only through encrypted
  `POST /omote-ha-proof` on isolated TCP 18082. The firmware verifies a
  valid uncompressed P-256 public point, exact binary frame length, DER
  signature, fresh transcript nonce and the **current Security2 session
  identifier** before returning the fixed encrypted receipt
  `OMOTE-LAB-HA-PROOF-OK-V1`.
- Replaying the signature in another Security2 session or altering the
  signed public key, nonce or version should fail. The existing per-message
  NVS quota remains in front of the SRP handshake.
- The exact framing, domain separator, endpoint and authorization flags are
  pinned in `Platformio/tools/config-v2-tests/sec2_lab_ha_proof_contract.json`.
  HACS `main` compares this contract in its CI. This is a **contract check
  only**; HACS does not yet connect to this experimental endpoint.

Optional invocation after intentionally flashing the lab environment,
making a verified full-flash USB backup and physically tapping
**Settings → SEC2 encrypted LAB test → Start lab**:

```sh
export IDF_PATH=/path/to/compatible/esp-idf
python3 Platformio/tools/config-v2-tests/sec2_lab_client.py \
  --host 10.0.10.121 --port 18082 --ack-lab-only --ha-proof
```

Successful output indicates encrypted SRP6a/AES-GCM communication and
possession of the **candidate** client's ephemeral private key. It does NOT
establish a persistent authenticated device identity or trusted HA
installation. It does not authorize any commands, config deployment,
credential writes, or firmware update. A production protocol must next
prove and pin a long-lived device key, prove the real HA key, bind both
to the PAKE transcript, require fresh touchscreen approval of the exact
HA peer fingerprint, and durably save/revoke trust through protected
storage. The lab's current 5-frame limit allows at most two ordinary
handshakes before it blocks additional attempts; it has no approved
physical reset mechanism yet.

## Security2 lab client prerequisites and safe diagnostics

If `sec2_lab_client.py` prints only
`FAIL: interoperability probe aborted (RuntimeError)`, that was an
older client hiding the exception's **stage**. Before opening another
lab window or spending the flash-persistent `omsec2lab/secmsgs` limit,
update the firmware repository and run:

```sh
cd Platformio
python3 tools/config-v2-tests/sec2_lab_client.py --check-prereqs
```

This test has **no network access or device-side effect**. It verifies
`IDF_PATH` explicitly points to a compatible **source checkout of
Espressif's ESP-IDF** with `tools/esp_prov`, generated protobuf modules
and Python dependencies. PlatformIO firmware compilation alone does
not configure this host-side provisioning client.

When `IDF_PATH` is missing, locate an existing ESP-IDF source checkout
or obtain one for the compatible firmware version; set, for example,
`export IDF_PATH="$HOME/esp-idf"`. Do not point it at the OMOTE
repository. Install the checkout's required Python packages in an
isolated Python environment. Repeat `--check-prereqs` until it prints
PASS **before** starting a lab window.

The updated client now reports the failing stage without exposing
PINs, server bodies, or cryptographic material:
`[prerequisites]`, `[connect]`, `[protocol-version]`,
`[validate-version]`, `[local-pin]`, `[srp-handshake]`,
`[encrypted-probe]`, `[ha-key-proof]`. A failure before the
`srp-handshake` stage does not spend the lab's Security2 message
quota. Do **not** repeatedly retry a failed SRP handshake without
reviewing the log, because this lab permits only five persistent
Security2 frames across reboots. The normal port-80 HACS API remains
available independently.

## Laboratory device software identity continuity (2026-11-05)

The feature branch has a third **encrypted, unprivileged** lab endpoint,
`POST /omote-device-proof`, in addition to the two previously proven
encrypted interoperability probes. It is opt-in in the `sec2_lab`
firmware environment only.

The locally touched **Start lab** action now prepares a versioned P-256
device **software** key. Its 98-byte NVS record is stored as a single
key under `omsec2id/labkey` and consists of:
`0x01 || 32-byte private scalar || 65-byte SEC1 uncompressed public key`.
Existing keys are validated against their derived public key before
they can be used; corrupted, truncated or wrong-version keys cause a
fail-closed startup rather than silent key replacement. A newly created
record is also verified through an immediate NVS readback. No network
request can create or rotate the key, and the in-RAM scalar is wiped
after the short-lived listener shuts down.

**Security limitation: standard Waveshare NVS is not guaranteed encrypted.**
Anyone able to read and duplicate the physical flash or a raw
full-flash backup may recover the laboratory private key. This
**cannot** establish a production-trusted hardware identity. Never
rely on this proof to authorize configuration writes, HA access or
OTA, and keep flash backups private. No eFuses are burned or
irreversible hardware configuration changes made.

The optional Python flag `--device-proof` sends
`0x01 || 32-byte cryptographically random challenge` after the actual
SRP6a/AES-GCM session is established. Firmware returns encrypted
`0x01 || 65-byte device public key || u16 BE DER signature length ||
DER ECDSA-SHA256 signature`. The signature covers exactly
`"OMOTE-SEC2-LAB-DEVICE-PROOF-V1" || Security2-session-id-u32-BE ||
challenge-32 || device-public-key-65`. The client checks the full
signature and prints a **lab-only** lowercase SHA-256 fingerprint of the
65-byte public key. The fingerprint is informational; HA does NOT
automatically trust or pin it. The exact wire record and disabled
authorization flags are pinned in
`Platformio/tools/config-v2-tests/sec2_lab_device_identity_contract.json`
and HACS CI.

After the new version has **compiled and been reviewed**, a controlled
follow-up (not an automatic reboot or new configuration deployment) can
use the existing lab screen and **one** additional handshake:

```sh
cd Platformio
python3 tools/config-v2-tests/sec2_lab_client.py \
  --host 10.0.10.121 --port 18082 --ack-lab-only \
  --ha-proof --device-proof
```

**Do not run this now merely to retest the previous two PASS stages.**
The lab still charges a flash-persistent five-Security2-message budget
that is never reset by reboot, so new testing must be coordinated.
The previous hardware success for SRP6a, AES-GCM and candidate HA
P-256 signatures remains valid.

The production milestone following this LAB-only continuity test is
properly protected private-key storage (eFuse-backed or appropriately
encrypted NVS with an established device protection model), verified
device provisioning/recovery, durable HA peer credentials and
fresh touchscreen comparison/approval of both identities within the
same authenticated session.

## Security warning

Espressif advisory GHSA-9r76-858f-v6jh (CVE-2026-45542, May 18, 2026)
describes a heap overflow in Security2 over **NimBLE BLE** in certain
ESP-IDF versions, including 5.3.5. The advisory explicitly states that
the HTTP/SoftAP transport bounds inbound messages, but a patched IDF
and independent request limits are still preferred before production.
Do not enable BLE Security2 on this branch.

Official references:
- https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/provisioning/protocomm.html
- https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/provisioning/provisioning.html
- https://docs.espressif.com/projects/esp-idf/en/v5.1.7/esp32s2/api-reference/provisioning/protocomm.html
- https://github.com/espressif/esp-idf/security/advisories/GHSA-9r76-858f-v6jh

## Build safeguards

The experimental Waveshare `sdkconfig.defaults.waveshare28v2` enables
`CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2` only to test compilation.
`WaveshareOmote/config_v2_sec2_compile_probe.cpp` deliberately invokes
no Security2 function, creates no listener and accepts no pairing request.
Compiling this code does NOT demonstrate security or interoperability.

The device must continue advertising:
`auth_mode=not-implemented`, `transaction_support=false`,
`rollback_support=false`, `write_protection_enabled=true`.
The existing port-80 API remains read-only. Never merge into stable or
turn on HACS deployment without explicit hardware approval.
