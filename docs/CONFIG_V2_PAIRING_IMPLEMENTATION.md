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


## Isolated persistent pairing security groundwork (feature branch)

The experimental firmware now includes an **NVS-backed enrollment attempt
budget** and a **versioned, single-HA public-key fingerprint pin**. Both are
low-level building blocks; neither can authenticate Home Assistant without
a reviewed PAKE implementation and genuine touchscreen confirmation.

- \`config_v2_pairing_attempt_budget.hpp\` specifies five **persisted,
  pre-verification** PIN attempts. The counter is atomically reserved before
  checking a candidate code. Once exhausted, a restart does not reset it.
  Only a physical touchscreen rearm after at least 120 seconds, or a verified
  + physically approved pairing **after durable trust storage**, can clear
  the counter. No remote endpoint can call the reset. The pure host test
  covers reboot, simulated concurrent callers sharing a store, NVS failure
  and \`millis()\` wraparound.
- \`config_v2_pairing_attempt_nvs.cpp\` implements the atomic, mutex-guarded
  read-increment-persist-readback using \`Preferences\` under the separate
  \`omotepair/attempts\` key. Storage failure means **no PIN comparison**.
  A production pairing coordinator must call this store on **every** PIN
  candidate before \`Window::submitVerifiedPeerCode\`. The preview has no
  network pairing candidate handler, so no such caller exists today.
- \`config_v2_pairing_trust_record.hpp\` strictly encodes a single
  lower-case 64-character SHA-256 fingerprint, prefixed with a format
  version; rejects malformed/corrupt records.
- \`config_v2_pairing_trust_nvs.cpp\` reads and performs a single-key,
  read-back-verified replacement of \`omotepair/haPeer\`. It never touches
  \`omotecfg/active\`, LittleFS scene data, Wi-Fi credentials or the
  firmware partition. Existing valid trust is not deliberately removed
  before replacement. The **caller must first prove** the session's
  cryptographic peer identity and consume a real touchscreen approval;
  the storage method cannot perform or infer these checks itself.
- All code is on \`feature/config-v2-transactional\` only. Host tests
  are wired into \`run-host-tests.sh\`. Hardware NVS behavior is **not**
  certified by these host tests.

**Still required:** a reviewed encrypted PAKE supported by ESP-IDF and HACS,
device key provisioning, candidate transport/session binding, authenticated
HA peer identity presentation, actual Approve/Reject touchscreen callbacks,
coordinator integration, credential revocation, failure injection and
real-Waveshare tests. Neither the six-digit PIN state machine, the NVS
attempt gate, nor a stored fingerprint is proof of secure pairing in
isolation. Wi-Fi/HTTP status and deployment capabilities remain unchanged:
\`auth_mode=not-implemented\`, \`transaction_support=false\`,
\`write_protection_enabled=true\`.

### Security limits

A physical attacker with flash/NVS write access may reset the attempt
counter or substitute a pinned key unless secure boot, flash encryption and
NVS key protection are reviewed and enabled. A six-digit PIN is low entropy:
the PAKE must hide the code from passive observers while the persistent
budget limits online guessing. Refuse secure pairing when storage operations,
cryptographic entropy, identity proofs or local confirmations cannot be
verified. Avoid making any claim that encrypted pairing is available until
end-to-end tests validate the firmware and HACS counterpart.


## Device-side coordinator and local touchscreen preview — next increment

The \`config_v2_pairing_coordinator.hpp\` template now combines the
previously independent policy and persistent stores. Its caller MUST first
supply a **cryptographically verified** encrypted provisional-session
handle; a six-digit code alone is never sufficient. The coordinator
requires a persisted attempt reservation before checking a code, binds
the pending HA public-key fingerprint and encrypted session identifier,
revalidates the same session on an actual touchscreen Approve gesture,
and only then calls the trust-store replacement method. It does not
have any HTTP endpoints, certificate/key generator or live cryptographic
verifier. Host tests use an explicitly fake verifier to validate the
orchestration, **not encryption strength**.

The Waveshare Settings list now exposes **HA pairing (preview)** on the
isolated feature branch. The local screen shows a six-digit temporary
code, and offers **Cancel**. A verified pending peer would cause the UI
to display all 64 hex characters of its SHA-256 public-key fingerprint
and show physically clickable **Approve / Reject** buttons. The only
current screen-owned window has **no secure session producer**, so it
can never reach a verified-peer state and cannot persist trust. It
continues to say **PREVIEW ONLY**. When an actual, reviewed PAKE
implementation is selected, integrate the coordinator with a
thread-safe UI dispatch and replace this preview-owned window with
the validated coordinator-owned window before enabling network
pairing. No remote API can currently open pairing, provide a
\`VerifiedPeer\`, or invoke touchscreen approval.

Espressif documents \`protocomm_security2\` as SRP6a + AES-GCM with a
Python-based provisioning client available for evaluation. Confirm
compatible security2 support, salt/verifier provisioning, session
lifetime/replay handling, persistent lockout, per-device public-key
pinning and memory use in this exact PlatformIO dual-framework build
before implementing or claiming a working encrypted pairing channel.

**Safety invariant:** \`auth_mode=not-implemented\`,
\`transaction_support=false\`, \`write_protection_enabled=true\`.
Firmware USB flashing, HA pairing and transaction tests must not begin
until the secure backend and HACS counterpart have passed review and
the Waveshare recovery plan is proven.
