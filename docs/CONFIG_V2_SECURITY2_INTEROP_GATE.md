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
after reserving a persistent NVS attempt-window counter, shows the local
six-digit code and opens port 18082 for up to two minutes. Closing or
cancelling the local page also stops the listener. Normal port 80 remains
unchanged and read-only.

No device identity, HA key-possession proof, persistent **per-SRP-candidate**
attempt lockout, peer-fingerprint approval, credential persistence or
remote-write endpoint is implemented in this laboratory transport. A six-digit code is NOT
sufficient protection for a production network service without those
safeguards. **Do not flash the lab image as a production remote.**

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

Only run this after the dedicated lab image passes CI, a verified full-flash
USB backup, and an explicit local Settings touchscreen action. The NVS
counter currently limits physical lab **window starts**, not individual
SRP candidate guesses within each window. The lab transport is therefore
NOT suitable for production pairing or Internet-exposed networks.
The user-selected final protocol also requires a stable device identity
and an HA public-key possession challenge bound to the encrypted session,
then a separate explicit on-device touchscreen confirmation of that peer.
It is not yet safe to expose a "Pair" button in HACS.

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
