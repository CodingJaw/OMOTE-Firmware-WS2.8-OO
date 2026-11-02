# OMOTE Config v2 — transaction and security contract (draft)

Status: **design contract only; no authenticated transport, pairing, or
network writes are implemented.** This document MUST NOT be interpreted as
deployment authorization. The currently shipped preview responds with
`transaction_support=false`, `auth_mode=not-implemented`, and
`write_protection_enabled=true`.

This is the successor to `omote-config-v1`. The accepted deployment model is
**single-active**, with no A/B slots, previous revision, trial boot, or
remote rollback. The user-controlled working firmware branch must remain
unchanged until signed off.

## Trust boundaries

1. The Waveshare MCU has its own device identity and persisted trust anchor;
   its silicon MAC/mDNS `id` is merely a discovery hint, never a secret.
2. HA must use an administrator-approved action to submit *each* deployment.
   Pairing does not authorize future deployments without fresh administrator
   consent. An authenticated HA service must attest that the specific admin
   authorized the exact manifest digest and device.
3. A separate **fresh touchscreen confirmation on the target device** must
   bind to the exact device, transaction ID, expected active revision and
   proposed revision. UI approval must never be reachable from HTTP/MQTT/BLE.
4. All writes, secret-bearing reads, pairing changes and admin attestations
   require an encrypted, mutually authenticated, device-bound channel.
   Unauthenticated LAN clients may use the existing restricted read-only
   discovery API; no CORS permissions are granted by the preview.
5. DeviceSettings, HA credentials, MQTT credentials, NTP/FTP settings, Wi-Fi
   credentials, pairing material, secrets and runtime caches are **outside**
   portable configuration manifests. A separate encrypted per-device opt-in
   API is a future independent requirement.
6. Stage files and immutable objects are untrusted until hashes and their
   full scene/page/command/image graph validate. Validation always takes
   place before any pointer update. A second validation is required when
   activating a previously prepared manifest.

## Pairing (to implement; cryptographic details not yet finalized)

- Establish the device's cryptographic identity through hardware UI
  verification. Do not accept only IP, hostname, mDNS TXT, or MAC address
  as proof of identity.
- Create or securely load the device's long-lived key and certificate/trust
  material in independent persistent storage. The private key must never be
  exportable through the configuration API, logs or portable profiles.
- Use a well-reviewed encrypted transport and a verified binding to that
  identity (e.g., device certificate pinned by HA after *local* fingerprint
  verification). Reject unverified/self-signed certificates without the
  verified physical pairing step. Never silently downgrade to HTTP for writes.
- Bind HA credentials to a specific paired device; provide a local,
  physically authorized way to revoke/re-pair when HA is replaced.
- Limit pairing attempts; expire pairing windows and ephemeral secrets; do
  not record passcodes, shared secrets, or tokens in serial output.
- Firmware must distinguish secure channel identity from HA-admin approval:
  possession of the pairing secret alone does not satisfy the per-deployment
  administrator confirmation requirement.

**Decisions still needing threat-model review:** TLS server strategy and
certificate provisioning on ESP32-S3, supported crypto library, fingerprint
display size and UX, certificate rotation, HA secure storage and revocation.
Do not improvise custom cryptography to speed implementation.

## Proposed API flow (no production handlers yet)

All transaction endpoints are on the mutually authenticated encrypted
service. In the preview, these endpoints MUST remain unreachable or return
a fail-closed error. The existing port-80 API remains read-only.

1. `POST /api/config/v2/transactions` (begin): supply expected active
   revision, candidate manifest SHA-256, canonical file index, and an
   authenticated per-deployment HA-admin authorization bound to the exact
   device and proposed digest. Reject stale expected revision (HTTP 409).
   Firmware, not HA, generates a cryptographically random 128-bit
   transaction ID. Only one active transaction per device.
2. `PUT /api/config/v2/transactions/{id}/objects/{sha256}` (chunk):
   supply exact expected offset/total length and at most 1024 bytes.
   Accept append at the current offset; permit identical retransmissions;
   reject overlapping or mismatched bytes. Files are written only under
   `/omote-v2/staging/{id}/`.
3. `POST /api/config/v2/transactions/{id}/validate`: verify every
   object length/SHA-256, immutable manifest digest and full graph.
   Recheck the persisted current active pointer and quotas; return a
   digest summary to be shown on HA and touchscreen. Validation does not
   change the active configuration.
4. Physical touchscreen presents the target, new revision, file count and
   action with **Approve / Reject**. Its approval expires in <=60 seconds.
   The UI may only approve the exact pending validated transaction.
5. `POST /api/config/v2/transactions/{id}/commit`: require still-valid
   authenticated session, fresh HA-admin attestation, still-matching
   expected active pointer, the one-shot local approval, fully verified
   stored manifest and objects. Change **one NVS `omotecfg/active` SHA
   pointer**, then require restart/reload from the newly pinned snapshot.
   Commit never silently falls back to legacy root.
6. `DELETE /api/config/v2/transactions/{id}` aborts staging without
   changing the active pointer. Expired/disconnected/revoked sessions
   invalidate outstanding approvals. Timeouts fail closed.
7. `GET /api/config/v2/transactions/{id}` returns bounded, non-secret
   status to the authorized HA peer (progress, validation errors, expected
   and proposed SHA, expiration). Do not expose credentials or raw secrets.

The physical approval must be bound to the authorized transaction tuple:
`(device_id, tx_id, expected_active_sha256, proposed_sha256)`.
The authenticated session, HA administrator approval and fresh local
confirmation are **three distinct conditions**; none can substitute for
the others. Persist replay protection or derive it from securely unique
nonces and transactional state; post-reboot replay must not reactivate an
already committed transaction.

## Limits and errors

Initial host-side policy: maximum 128 manifest paths, 256 KiB per file,
2 MiB total manifest content, and 1024 bytes per upload chunk. Enforce a
nonzero free-space reserve (currently 128 KiB minimum), with allowance for
active objects, staging overhead and FS metadata. These are **provisional**
until physical LittleFS and heap tests. Never allocate request-sized
untrusted JSON or multipart bodies on the UI thread.

Use stable machine-readable error codes and no credential-bearing details.
Suggested mappings:

| HTTP | Machine code | When |
| --- | --- | --- |
| 400 | `invalid_request` | Invalid schema, path, checksum or chunk |
| 401 | `unauthenticated` | Missing/invalid secure session |
| 403 | `approval_required` | Missing HA-admin or touchscreen authorization |
| 409 | `active_revision_conflict` | Persisted active pointer differs |
| 409 | `transaction_conflict` | Other transaction, replay or offset mismatch |
| 410 | `transaction_expired` | Stale transaction or local approval |
| 413 | `quota_exceeded` | Staging/file/manifest limits exceeded |
| 422 | `invalid_graph` | Missing scene/page/command/image dependency |
| 507 | `insufficient_storage` | Reserve cannot be maintained |

Never return a positive `transaction_support` capability until the
entire secure service, recoverability and physical test gates pass.

## Recovery contract

- Recovery UI and Wi-Fi provisioning must not parse untrusted scene data.
  Preserve independent Wi-Fi/pairing settings and allow target-local
  re-authorization of network changes; public open-AP provisioning must not
  become an unauthenticated permanent settings-write bypass.
- A failed stage/upload/validation leaves the active pointer untouched.
- A committed but invalid manifest enters independent recovery. HA may resend
  a complete new config **only through the same authenticated, physically
  approved protocol**; there is no automatic rollback.
- Do not recommend `uploadfs`, flash erase or manual NVS pointer edits as
  the routine recovery path; those require a verified external restore
  procedure and explicit operator decision.
- Prove reset/power-cut behavior, physical UI availability, independent
  reconnect, and HA redeployment on the real Waveshare before deployment.

## Safe integration order

1. Host regression tests and a failing-closed port-80 audit.
2. Independently implement and test device identity, pairing and encrypted
   transport with no configuration writes exposed.
3. Add authenticated read-only session inspection and replay controls.
4. Connect bounded staging/validation and admin attestation on a non-UI task.
5. Connect touchscreen approval and single active-pointer commit.
6. Verify independent recovery, memory/latency, power interruption, and
   manual full-flash restoration on hardware.
7. Only then advertise transaction support and enable HACS deployment
   behind a per-device approval flow. Do not merge into the stable branch
   before explicit permission.
