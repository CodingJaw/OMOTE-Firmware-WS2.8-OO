# Config v2 — user-approved pairing contract and implementation status

Status (2026-11-02): **local-only pairing UX and policy are under
development; remote pairing is NOT enabled.** The feature branch must
continue to advertise `auth_mode=not-implemented`,
`transaction_support=false`, and `write_protection_enabled=true`.
No credentials are added to portable manifests or exposed over port 80.

## Approved operator choices

1. Pairing entry: **both** touchscreen Settings and a physical key chord.
2. Verification: show a **six-digit code locally on the remote**, enter it
   inside Home Assistant, then confirm the specific HA peer on the remote
   touchscreen.
3. Replacement or reinstall of HA: **pair again**, preserve the existing
   configuration, scene JSON, images, active NVS manifest pointer and other
   device settings. The old trust record may only be replaced after the new
   pairing is fully authenticated and locally approved.

The current Waveshare adapter has **no external keypad wired**. The
`Home + Back` long-hold combination is a provisional software mapping
for the future keypad and cannot yet be physically exercised. The
touchscreen Settings entry is usable on existing hardware.

## Implemented (local preview only)

- `config_v2_pairing_policy.hpp`: uniformly selected six-digit PIN
  from CSPRNG-supplied 32-bit words (rejection sampling), 120-second
  pairing window, 60-second post-code local approval deadline,
  maximum five incorrect guesses, in-memory 120-second cooldown,
  peer public-key fingerprint + encrypted-session binding, and a
  one-shot local confirmation gate. No credentials are saved.
- `PairingPreviewPage`: opt-in local Settings page, visibly labeled
  **PREVIEW ONLY**; shows the ephemeral six-digit PIN and its expiry.
  The code cannot be entered into HA because there is no secure
  pairing server or client yet. On page destruction it is cleared.
- `config_v2_pairing_entry.hpp`: local chord detector, latched until
  key release; current hardware has no producer for keypad events.
- Host tests: PIN format (including leading zero), unbiased sampler
  rejection, code expiry and timer wraparound, brute-force cooldown,
  peer/secure-session requirements, approval replay and chord behavior.

**Important:** The `VerifiedPeer` struct is a *trusted manager input*,
not proof that the connection is encrypted or authenticated. Network code
must never construct or submit it until a cryptographic key exchange
has verified possession of the peer private key. The PIN by itself does
not authenticate Home Assistant.

## Cryptographic implementation candidate

Evaluate Espressif's existing `protocomm_security2` (SRP6a password
authenticated key agreement + AES-GCM session encryption) rather than
implementing custom cryptography. Espressif documents `protocomm` as
supporting application-specific endpoints, including over SoftAP/HTTP
and BLE; compatibility with this exact dual-framework PlatformIO
toolchain and a Home Assistant Python client is not established.

Official ESP-IDF references:
- https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/provisioning/protocomm.rst
- https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/provisioning/provisioning.rst

Before implementation, verify `CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2`,
server dependencies, verifier/salt provisioning and cleanup, the exact
ESP-IDF version on the Waveshare target, and suitable Python client
libraries. Avoid `security0` and avoid an unauthenticated X25519
handshake without possession proof. No downgrade to an unauthenticated
HTTP endpoint is permitted.

Six digits provide limited entropy. A PAKE prevents exposing the PIN
itself to a passive network observer but **does not eliminate online
guessing**. Rate limits must survive reboot, be enforced at the session
service before PIN checking, and be verified under concurrent requests.
The in-memory policy is **not** sufficient production protection.

## Proposed secure enrollment flow (not yet running)

1. The operator locally enters pairing mode through Settings or the
   physical chord. Only the remote renders the random six-digit code.
2. HA, when an HA administrator starts device setup, establishes the
   supported PAKE with the remote; entry of the displayed PIN is confined
   to the HA UI and secure key-exchange code. Do not transmit the PIN
   in plaintext query strings, headers, JSON or logs.
3. HA and the remote bind the ephemeral encrypted session to the peer
   device public key, and show the specific HA instance identity and
   fingerprint on the physical touchscreen.
4. User taps **Approve** on the remote. Wrong code, timeout, close/cancel,
   mismatched peer fingerprint or user rejection aborts pairing.
5. Only *after* the approval is consumed does the firmware durably store
   the validated peer identity in a separate protected namespace. Do
   not mutate `omotecfg/active` or LittleFS portable config.
6. HA securely persists the paired device identity and credentials. A
   replaced/reinstalled HA instance must go through the entire process
   again; do not accept a stale pairing token for credential replacement.
7. Pairing authorizes connection but not deployment. Each configuration
   transaction still requires separately verified HA-admin consent and
   **a new on-device touchscreen approval**, plus full validation.

## Gates before enabling any network pairing route

- [ ] Select a reviewed PAKE implementation supported by BOTH firmware
      and the HACS Python runtime.
- [ ] Bind ephemeral session to device identity and HA peer public key;
      document cert/key generation, rotation, and revocation.
- [ ] Secure, atomic trusted-peer storage with no configuration erase.
- [ ] Persistent attempt-counter protection and hard network rate limits.
- [ ] Peer-identity confirmation screen with explicit Approve/Reject.
- [ ] No unauthenticated write route and no PIN/readout over the v1 API.
- [ ] Real Waveshare and HA test of pairing, denial, timeout,
      interruption/reboot, reinstallation and credential replacement.
- [ ] Explicit hardware sign-off before activating transactions.

The current local preview must never claim a successful pairing or
enable deployment. Issue #1 remains open.
