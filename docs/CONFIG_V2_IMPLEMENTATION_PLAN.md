> **SUPERSEDED (November 1, 2026):** The user has cancelled A/B
> configuration switching and automatic configuration rollback. This file is
> retained for historical context. The CURRENT firmware design and handoff
> requirements are in
> [CONFIG_V2_SINGLE_ACTIVE_DESIGN.md](CONFIG_V2_SINGLE_ACTIVE_DESIGN.md).
> Any A/B, previous-generation, boot-trial or rollback requirements below
> MUST NOT be implemented.
>
# OMOTE config v2 — implementation plan (issue #1)

Status: **development, incomplete, not deployable**. The user authorized implementation on this
branch only, with hardware tests before any merge into `waveshare28v2-port`.
Do not flash this branch as a replacement for a proven working remote before a tested recovery procedure exists.

## User decisions (2026-10-31)

- Internal LittleFS is the only required storage. An SD card is never required.
- A/B means the active and previous confirmed configuration generation. A staging
  area is additionally required *during* a transaction.
- Pairing is manually approved on the touchscreen with a one-time code.
- Pick encrypted, authenticated transport after real ESP32-S3 memory/latency testing.
- Disable **all** unauthenticated v1 HTTP mutation endpoints; v1 reads of
  non-sensitive configuration remain available.
- Safe reboot activation is the default; hot reload is an optional validated
  optimization only after the complete UI scene graph can be switched coherently.
- Automatically roll back **only** after local boot/config/UI-health failure,
  never because Home Assistant or Wi-Fi is unavailable.
- Authenticated v2 deployment should eventually support sensitive configuration
  (DeviceSettings, HA credentials, MQTT/FTP credentials) with explicit per-device
  approval and extra secret-handling protections.
- Shared profile definitions may be reused across devices, but each device
  requires an explicit, independently authenticated deployment approval.
- Touch responsiveness and scene switching have priority over upload throughput.
- The remote must remain usable **without Wi-Fi or Home Assistant**. BLE and IR
  must remain decoupled from network initialization. The current Waveshare
  hardware adapter still has an IR placeholder; this plan does not claim IR/BLE
  is already implemented on that port.

## Additional user decisions — October 31, 2026

- **USB testing authorized on the current Waveshare** for controlled trials of
  the `feature/config-v2-transactional` branch. Do not merge into the stable
  `waveshare28v2-port` branch or automatically flash firmware from HACS.
  Capture a verified backup and preserve a known-good firmware/recovery path.
- **Every individual deployment**, even after device pairing, requires an HA
  administrator confirmation AND a new explicit physical touchscreen approval
  on the *target* remote. Previous confirmations must never carry over.
- **Automatic mDNS discovery** via `_omote._tcp.local.` with TXT `id`, `api`
  and `model`, plus manual IP/hostname fallback. Discovery is *not* pairing,
  authentication, proof of identity, or approval for a deployment.
- User confirmed that the existing Home Assistant integration's read-only
  backup and disconnect/reconnect behavior both work.

### Current implementation of these decisions (not deployable)

The preview firmware advertises an additional `_omote._tcp` mDNS service
with `id=<12 lowercase efuse-MAC hex>`, `api=omote-config-v1`, and
`model=waveshare28v2`. This runs alongside its existing `_http._tcp`
announcement only when Wi-Fi is connected. The TXT ID is an unauthenticated
discovery hint and must not become a permanent trusted device identity.
mDNS cannot control IR/BLE or block the LVGL/UI task.

`config_v2_local_approval.hpp` is a pure C++ gate for future deployment
and rollback requests. Its host tests verify a fresh request-specific local
approval, expiration, mismatch rejection, cancellation, replay rejection,
and one-time consumption. It requires a fresh 32-hex transaction identifier
and 64-hex SHA-256 proposal/base revision. No HTTP endpoint, GUI touchscreen
callback, persistent authorization secret, or live commit handler is connected
to this gate. In particular, `confirmLocalTouch` MUST only be called by a
trusted **local touchscreen gesture**, never by any remote network API.
The network side must independently verify authentication and administrator
approval; the gate is a necessary, not sufficient, commit precondition.

## Current implementation in this branch — stage 0, fail-closed

The `OMOTE_CONFIG_V2_PREVIEW` build flag is enabled only for
`waveshare28v2_omote`. It:
- Keeps the v1 status and public, non-secret filesystem read endpoints.
- Responds HTTP 403 to all registered v1 HTTP mutation routes
  (file write/delete, device settings, reboot, sync, IR learn start/stop).
- Returns HTTP 403 from `GET /api/device/settings`, because the remaining
  fields can be private and cannot be safely redacted by a two-field list.
- Restricts anonymous file reads and directory listings using the host-testable
  `config_v2_path_policy.hpp` allowlist; it blocks `HaSettings.json`,
  `DeviceSettings.json`, `mqtt.json`, `ftp.json`, state caches and internal paths.
- Emits no cross-origin permissions in the preview build.
- Does not service the legacy FTP server (an alternate write mechanism).
- Adds read-only `GET /api/config/v2/capabilities` returning
  `transaction_support=false`, `rollback_support=false`,
  `auth_mode=not-implemented`, and zero deployment limits.
- Adds read-only `GET /api/config/v2/storage/status` showing raw LittleFS
  total/used/estimated free bytes, validity of two redundant NVS selector slots,
  and whether a selector has been initialized. No mutation or generation
  activation occurs when this endpoint is called.
- Anonymous file-tree listing skips staging/blob directories instead of walking
  them and filtering afterwards. This avoids large scans in the touch UI.

These controls **do not** constitute a secure v2 implementation; do not enable
HACS deployment based on a version string. The stock browser editor's remote
write and cross-origin features are intentionally unavailable in the preview.
Serial `uploadfs` remains the physical recovery path. All other firmware
targets retain existing behavior until reviewed.

## Architecture: content-addressed generations in LittleFS

The existing Waveshare partition table contains a 0x5E0000-byte (~5.875 MiB)
LittleFS partition. Capacity must be tested against actual scene assets and
flash erase overhead.

A/B refers to two *confirmed configuration revision pointers* rather than a
requirement to duplicate every file. Proposed layout:

```text
/littlefs/
  Scenes.json                   # legacy root during migration / recovery
  Scenes/ Pages/ Commands/ ...  # existing working files
  omote-v2/
    objects/<sha256>            # immutable, content-addressed file bodies
    manifests/<rev>.json        # immutable path -> length, sha256, type map
    staging/<transaction-id>/   # interrupted transactions / temporary objects
```

Two redundant NVS records select the active revision, previous confirmed
revision, pending trial revision and monotonic record counter. Verify their
checksums and referenced manifests before selecting a revision. The initial
pure state machine lives in `config_v2_generation_selection.hpp`, with tests
in `tools/config-v2-tests/generation_selection_test.cpp`. It is deliberately
not yet connected to persistent NVS or boot; callers will need to persist
each state change before starting the selected UI. The state machine is a
component, not a completed power-loss-safe A/B implementation. Do **not**
assume NVS writes, FAT directory moves, or one-file rename make an entire
JSON graph atomic. Never modify an object that belongs to an active manifest.

This approach shares unchanged blobs across generations, reducing flash wear
and storage compared with copying entire directory trees. During deployment,
both confirmed generations plus staged new objects must remain recoverable.
Compute conservative free-space requirements **before** beginning an upload.
Do not erase the last confirmed generation simply to fit a new upload.

### Migration from current root files

Current code uses fixed `FS_PATH` and `std::ifstream` paths:
- `JsonHomeScreen.cpp` loads `Scenes.json` and individual scenes.
- `JsonPage.cpp` loads page JSON; widgets can load image assets through LVGL.
- `Command.cpp` loads command JSON.
- `HaRuntime.cpp`, `device_settings.cpp`, schema and logging/network
  components read and write their own JSON or Preferences files.
- `config_http.cpp` directly operates on `LittleFS`.

Introduce a single configuration resolver returning a pinned generation for a
whole scene load; update **all** readers, asset paths, and relevant writers.
A scene must never resolve half its files from A and half from B. Keep the
existing root configuration untouched until a validated v2 generation has
booted and been confirmed; if space cannot accommodate migration, refuse it
without destructive formatting. Recovery UI must boot independently of a
valid user scene manifest or network connection.

### Persistent boot metadata — development implementation

`config_v2_boot_record.hpp` encodes a versioned 216-byte selector with
active, previous and pending SHA-256 revision identifiers, an increasing
sequence counter, trial-boot count, and CRC-32. It rejects malformed/duplicate
digest IDs, unsupported versions and impossible state combinations. A host
regression test simulates incomplete writes and bit corruption, verifying the
previous valid selector remains selectable.

`config_v2_boot_nvs.cpp` adds an ESP32 Preferences adapter using two separate
NVS entries under namespace `omotecfgv2`. The write path verifies the
expected current slot/sequence, alternates to the other slot and reads back the
result before reporting success. **It is not wired into boot or any HTTP write
handler.** The read-only diagnostics endpoint is the only current consumer.

The structural `config_v2_manifest_index.hpp` precheck allows up to 128
configuration JSON files, each at most 256 KiB, totalling at most 2 MiB.
These are **initial development limits, not advertised supported v2 deployment
limits**. It requires `Scenes.json`, rejects duplicate/unsafe paths and
invalid SHA-256 identifiers, and flags credential-bearing files. This DOES
NOT parse JSON, verify any file bytes or SHA-256 digest, resolve cross-file
references, or decide that an actual configuration is safe to activate.
Non-JSON assets require a separate explicit, validated policy.

### Firmware-only validation and storage components (inert)

The following are now source files on this branch. They **do not**
implement the secure upload API, modify active config paths, or authorize
any network mutation. They require CI build verification and on-device
benchmarks before use in a live transaction:

- `config_v2_sha256.hpp`: streaming SHA-256 with fixed working memory; host
  vectors include empty input, "abc", 55-byte padding boundary and 1 million
  bytes. Hashes are for integrity, not authentication.
- `config_v2_stage_policy.hpp`: 1 KiB maximum chunk; 256 KiB initial per-file
  limit; strict 128-bit device-generated transaction ID format; offset
  conflict/idempotent retry detection and 128 KiB conservative free-space
  reserve. A full transaction must also account for manifest size, namespace
  metadata, flash wear, active/previous generations and temporary data.
- `config_v2_littlefs_objects.hpp/.cpp`: optional verified staging under
  `/omote-v2/staging/<tx>/<digest>.part` and immutable publication into
  `/omote-v2/objects/<digest>`. It never truncates root active JSON or existing
  objects. These functions are **not called** by HTTP handlers or boot.
  `LittleFS.open("r+")`, flush and rename behavior needs actual hardware tests.
- `config_v2_manifest_format.hpp`: strict canonical JSON manifest with
  lexicographically ordered logical paths, explicit format version and
  immutable SHA-256 revision over exact serialized bytes. Decoder checks
  structural index limits. A committed manifest must be stored and verified
  by the future generation transaction manager.
- `config_v2_graph_validator.hpp`: checks content sizes and SHA-256 hashes
  over exact bytes, JSON with comments using RapidJSON, scene registry,
  referenced scene/page/command files, explicit command names/prefixes,
  supported widget types and referenced image assets. It is currently a
  **synchronous, in-memory helper**; production validation must be broken
  into bounded work on a non-UI task, including streaming reads/hashes and
  careful heap limits. It does not validate semantic IR protocol data,
  hardware availability, or all optional device schema fields.

Important: allowlisted `Images/*.png`, `*.jpg`, `*.jpeg` and `*.bmp`
are valid configuration assets; non-JSON asset support must be reflected in
HACS packages. Other file categories remain denied.

Currently there are no authenticated v2 transaction handlers, touchscreen
pairing, TLS provisioning, persistent manifest writer, pinned runtime resolver,
or activation/rollback execution. `transaction_support` and
`rollback_support` MUST stay false until all those pieces are implemented
and hardware-tested.

### Pending/confirmed boot protocol

1. Begin: bind transaction to device ID, build ID, active revision, unique
   request ID and signed/authenticated session. Check space, path policy,
   quotas, and concurrency; return conflict if the active revision changed.
2. Stage: bounded chunks into isolated immutable temporary objects; validate
   offsets, per-file SHA-256, length, duplicate retries, and total quotas.
3. Validate: parse JSON-with-comments using the actual firmware parser;
   verify every scene/page/command reference, widget compatibility,
   asset references, required settings and supported hardware targets.
4. Commit: only after user confirmation, write the validated immutable
   manifest, then persist a **pending** revision using redundant, checksummed
   NVS selection records. Reboot by default.
5. Boot: before rendering scenes, verify selected manifest and required
   objects. Validate the UI-load path; treat boot crashes/watchdog failures
   with a bounded boot-attempt policy, avoiding endless reboot loops.
6. Confirm: only after local UI startup and stability checks, move the
   previous active generation into the rollback slot. Home Assistant's
   presence is not a health condition.
7. Recover: if the pending generation fails locally, select the last
   confirmed generation. If both are damaged, use the embedded/root recovery
   configuration and display local recovery instructions.
8. GC: remove orphan staging and unreferenced objects **only** when no
   active, previous or pending manifest references them.

Abort must never alter the active pointer. Rollback is a separate, confirmed,
authenticated operation. Repeated commit requests must be idempotent and
bounded by request ID and exact digest.

## Pairing, secrets, and transport — not implemented

Pairing approval must be explicitly confirmed on the touchscreen. The one-time
code alone is not equivalent to encrypted authentication over plaintext HTTP.
Evaluate at least:
- TLS server + pinned device certificate and ephemeral challenge; and
- a reviewed mutual-authenticated encrypted channel with SAS/PAKE-style
  pairing (if available in the tested ESP-IDF build).

Measure handshake heap/PSRAM usage, CPU time, connection impact on LVGL,
and key storage/rotation. Require device-bound keys, nonce replay protection,
failure rate limits and revocation. No bearer secrets in URLs, logs or query
strings. No unauthenticated mutation endpoint, CORS bypass or fallback to
plaintext if a secure session cannot be established.

The user approved *eventual* deployment of credential-bearing files, but the
HACS protocol v0.1.0-dev10 explicitly excludes them. Coordinate a separate
HACS review to protect exported backups, redact diffs and logs, and require
per-device explicit secret selection. Pairing secrets, private keys and Wi-Fi
NVS credentials must **not** be included in reusable shared profiles. Moving
Wi-Fi NVS credentials requires a distinct physically recoverable flow.
Sensitive documents must never appear in the public v1 read surface.

## UI responsiveness and network independence

- Boot configuration and local UI without waiting for a network connection,
  pairing, discovery, HA handshake, or a storage deployment session.
- A network outage cannot cancel scenes, disable physical controls, or trigger
  rollback. IR/BLE drivers and UI events must not call blocking deployment code.
- Hashing, validation and file writes should run as low-priority, bounded work
  with progress/checkpoints. Keep LVGL ownership on its UI thread; avoid long
  `WebServer.handleClient` stalls, large contiguous allocations and flash
  erases in the touch-critical path.
- Prefer streaming JSON validation and file hashing; validate static references
  off the UI path. Preserve image cache and touched widget lifetimes.
- Benchmark tap latency, swipe latency, LVGL frame time, free heap and PSRAM
  during idle, Wi-Fi offline, pairing, transfer, validation and commit.

## Gate conditions before HACS can deploy

1. v2 capabilities affirm authenticated transaction + rollback support and
   positive limits; the preview explicitly does not.
2. Device-bound secure session passes pairing, replay and revoked-peer tests.
3. No unauthenticated filesystem or credentials leakage via HTTP/FTP/other
   network services.
4. Power removal at each stage/selection/boot/rollback point does not produce
   mixed active configuration or destroy the last good generation.
5. Reject nonexistent dependencies, malformed JSON-with-comments, unsupported
   UI widgets, oversized assets, stale base hash, unexpected retries and lack
   of flash space; return unambiguous status without partial activation.
6. The hardware boots and operates without Wi-Fi, HA or microSD; recovery is
   possible using serial `uploadfs` and the local recovery UI.
7. The current config editor has an authenticated v2 migration or a deliberate
   read-only compatibility mode; no hidden re-enabling of v1 mutation.
8. Each device requires explicit approval for shared-profile deployment.
9. Document and test BLE/IR offline behavior for the specific hardware adapter;
   do not infer functionality from component presence alone.
10. Obtain explicit review before merging into `waveshare28v2-port`.

### Outstanding stages

- [x] Dedicated read-only `_omote._tcp.local.` mDNS discovery announcement
  in the preview build only (pending build/hardware verification).
- [x] Host-testable per-deployment physical approval gate (not yet connected
  to touchscreen UI, pairing, or authenticated commit).
- [ ] Actual local touchscreen confirmation UI and secure transaction-to-device
  identity binding, verified in physical hardware tests.

- [x] Dedicated development branch and locked-down v1 preview.
- [x] Read-only negative-capabilities response.
- [x] Pure path policy and initial host regression tests.
- [x] Standalone A/B trial selection logic and host tests.
- [x] Checksummed dual-slot boot selector encoding and read-only NVS inspection
  (boot and activation are not yet integrated).
- [x] Preliminary bounded manifest-index validation and host regression tests.
- [x] Portable streaming SHA-256 and known-answer tests.
- [x] Canonical immutable manifest encoder/decoder.
- [x] Firmware-only staging, chunk offset/idempotency rules and object-store
  implementation (not wired to any network endpoint).
- [x] JSON-with-comments graph integrity/reference validator and host tests
  (not integrated into activation; hardware latency not yet measured).
- `config_v2_local_approval.hpp`: per-request, time-limited one-time local
  touchscreen approval gate with host tests (not wired into a live GUI/HTTP
  transaction). It is separate from the first-time pairing process.
- [x] Read-only LittleFS capacity diagnostics; private v1 settings reads denied.
- [ ] On-device compilation and network security inspection.
- [ ] Config storage resolver and migration with immutable objects.
- [ ] Redundant NVS boot records, generation selection and automatic recovery.
- [ ] Transactional stage/validate/commit/rollback and crash simulation.
- [ ] Secure pairing UI, encrypted channel and rate-limited authentication.
- [ ] Sensitive-data management and HACS protocol changes.
- [ ] Browser editor v2 migration.
- [ ] Physical Waveshare power-cut and latency acceptance tests.
- [ ] Explicit approval before merging.

## mDNS discovery acceptance procedure (read-only)

Once the host tests and the experimental Waveshare build succeed, flash only
the feature branch using a controlled USB test. In Home Assistant, with
`zeroconf` enabled, the firmware should appear as an **OMOTE Manager discovered
device** and require explicit human confirmation. Manual setup using the
remote's LAN IP or `omote.local` must still work. A previously installed
device under the same IP or hostname must not be added twice. No remote
configuration write route is enabled by mDNS.

Test both: (1) same known remote discovered repeatedly, and (2) a restart
or Wi-Fi outage. Verify that scene switching and touchscreen input remain
functional offline; discovery must not change control behavior. Do **not**
claim device pairing or deployment authorization from this advertisement.
The local approval state machine cannot be exercised through a live GUI until
a properly authenticated firmware transaction UI is implemented.

## Stage 1 device test procedure (read-only)

> The `waveshare28v2-port` branch remains unchanged. The Config v2 branch is
> **not** a crash-safe deployer. This stage adds no authenticated upload or
> generation switch. Do not invoke `uploadfs` for this test.

```bash
git fetch origin
git switch feature/config-v2-transactional
git pull --ff-only
cd Platformio
bash tools/config-v2-tests/run-host-tests.sh
~/.platformio/penv/bin/platformio run -e waveshare28v2_omote
~/.platformio/penv/bin/platformio run -e waveshare28v2_omote -t upload \
  --upload-port /dev/ttyACM0
~/.platformio/penv/bin/platformio device monitor -p /dev/ttyACM0 -b 115200
```

In a second terminal, only while connected to the same trusted LAN, set
`OMOTE_IP` to the actual device address and check:

```bash
curl -sS "http://$OMOTE_IP/api/config/v2/capabilities"
curl -sS "http://$OMOTE_IP/api/config/v2/storage/status"
curl -i "http://$OMOTE_IP/api/device/settings"    # expect HTTP 403
curl -i -X POST "http://$OMOTE_IP/api/fs/delete?path=Scenes.json"  # HTTP 403
```

The status endpoint should report `generation_activation_enabled=false`.
For a device that has never used the new v2 metadata namespace, expect
`selector_initialized=false`. Read-only `Preferences.begin` can report
`nvs_readable=false` for a missing namespace; it is not by itself a fault.

Acceptance checks: unplug Wi-Fi/router or disable the access point and verify
the local touchscreen UI and scene transitions still work. Compare free heap,
tap and swipe latency before and after. This test does not verify future IR or
BLE functions of the Waveshare port because those drivers still require
separate implementation and/or verification.

## HACS handoff boundary

HACS `develop` currently supports read-only v1 status/inventory, draft editing,
live drift review, and download-only backup/rollback ZIP. No change to HACS
is needed to exercise these firmware-only components. The future HACS issue
must cover:

1. Treat `omote-config-v2-preview` as **not deployable** and require secure
   capability negotiation with positive authentication/transaction/rollback
   flags and per-device limits.
2. Implement touchscreen-authorized encrypted pairing and securely persist
   device-specific trust material in HA's private storage, not in profiles.
3. Convert reviewed full manifest into canonical generation format and
   upload immutable content using 1 KiB bounded idempotent chunks; support
   explicit per-device consent, conflicts, retries, cancel and stale base
   revision checks. Chunk/size values are development defaults, not yet
   stable public firmware capability guarantees.
4. Include image assets and credential-bearing documents only with
   extra opt-in, export encryption/private storage and log redaction.
   Currently HACS excludes these files, as required by its safety policy.
5. Commit only after validation and user confirmation, then observe local
   health/active SHA-256 and permit authenticated rollback.
   Wi-Fi downtime must **not** trigger a firmware rollback.
6. Migrate the browser editor away from v1 write APIs and CORS assumptions.
7. Keep a kill switch. No actual writes until authenticated firmware v2
   functionality and Waveshare physical power-loss tests pass.
