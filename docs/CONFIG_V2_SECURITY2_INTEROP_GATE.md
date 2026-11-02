# Security2 interoperability gate — pairing candidate (NOT ENABLED)

Status: **compile-time feasibility evaluation only**, November 2, 2026. This
document does not authorize network pairing, unauthenticated provisioning,
secret access, configuration writes, or firmware deployment.

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
