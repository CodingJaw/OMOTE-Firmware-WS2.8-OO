# OMOTE Config v2 — single-active deployment design

**Decision (2026-11-01): Retire A/B configuration generations and automatic
configuration rollback.** Home Assistant is the source of truth and may resend
a reviewed configuration. This design supersedes all conflicting A/B,
`previous_revision`, `trial_boot`, `rollback`, and dual-configuration
requirements in the older `CONFIG_V2_IMPLEMENTATION_PLAN.md` and issue
comments. The older plan is retained as historical development context.

**Status: design revision; firmware remains preview-only and non-deployable.**
The working `waveshare28v2-port` branch and HACS code have NOT changed.
There is no authenticated upload/activation implementation yet.

## Scope retained

- Firmware boots the touchscreen and **local recovery interface** without
  requiring HA, Wi-Fi, usable Scenes.json, or an SD card.
- Use LittleFS staging, bounded chunks, SHA-256 integrity checks, strict
  allowlisted relative paths, JSON-with-comments parsing, scene/page/command
  and image reference validation, quotas and free-space reserve.
- Authenticate and encrypt transport, pair with a device-bound trust identity,
  guard against replay, support manual IP fallback and mDNS discovery.
- A **new HA administrator approval AND new target touchscreen approval** is
  required for every single deployment; pairing alone never grants deployment.
- Keep old v1 HTTP mutations and legacy FTP disabled; anonymous sensitive
  settings reads are forbidden. Port 80 v1 remains read-only until replaced.
- Check the expected active revision before staging/commit; reject concurrent
  or stale transactions. Allow idempotent upload retries, abort and explicit
  recovery-mode redeployment.
- Preserve touch, scene responsiveness and standalone operation. Do not
  represent IR/BLE as hardware-tested on the current Waveshare adapter.
- Keep `DeviceSettings.json`, `HaSettings.json`, credentials and pairing
  keys out of portable profiles and unauthenticated reads. Sensitive changes
  need separate explicit per-device opt-in and encrypted handling.

## Simplified storage and commit model

There is **one current user configuration**, not confirmed A/B versions.

```text
/littlefs/
  Scenes.json, Scenes/, Pages/, ...       # legacy bootstrap; untouched for now
  omote-v2/
    staging/<transaction-id>/             # incomplete, never active
    objects/<sha256>                      # verified immutable file bodies
    manifests/<sha256>.json               # complete validated file index

NVS (proposed, not yet implemented):
  single active-manifest digest           # current configuration ONLY
  no previous-generation or pending-trial state
```

A manifest identifies **all** files for one logical configuration. The
loader pins its revision for the complete scene, so a scene cannot read
half-written files. The actual runtime loader still uses legacy root paths
and is NOT yet switched to this model.

1. Pair and establish an authenticated, encrypted session.
2. Receive HA-admin-reviewed proposed manifest and create a device-bound
   staging transaction. Never write active root paths while staging.
3. Verify each file's exact bytes/size/SHA-256 and full cross-file graph.
   Reject invalid, oversized, incomplete or stale proposals.
4. Publish immutable objects and their canonical manifest, then perform **one
   small, durable active-revision change**. Determine/test the actual NVS
   persistence semantics and flash interruption behavior before shipping.
   This is a **single current revision pointer**, not an A/B selector.
5. On reboot, the runtime loads only that manifest. Never mix it with partial
   staging or silently fall back to old files on missing dependencies.
6. After an interrupted staging or rejected commit, keep the *currently
   published* snapshot unchanged. A completed commit selects the new snapshot.
7. After a bad committed configuration, show a **self-contained local recovery
   screen**, preserve networking/pairing outside user scene JSON and allow an
   explicitly approved HA redeployment. **No automatic restoration of an old
   configuration is promised.**
8. Clean up unreferenced staging/objects only when not required by the current
   manifest or active transaction. Do not delete the only usable active copy
   to make room.

### Why we cannot simply overwrite each live JSON file

Home Assistant can resend only **if the remote remains reachable**. If an
interrupted update corrupts `Scenes.json` or a Wi-Fi setting and the UI/network
cannot start, HACS cannot repair it remotely. Therefore *staging, validation,
atomic activation and an independent recovery path* remain required, even
without A/B rollback.

### If the new configuration is bad

- Do **not** force an automatic A/B rollback or retain a managed previous
  generation. No A/B boot attempts, trial counters, prior pointer or remote
  rollback endpoint.
- Recovery UI must start from firmware-independent resources without parsing
  the broken user scene tree.
- Preserve safe Wi-Fi settings in independent storage; where normal WLAN
  fails, offer a locally authorized setup/reconnect path (details to design
  and validate, not implemented).
- HA must be able to pair/reconnect and resend the configuration through the
  **same encrypted, physically approved transaction protocol**. No emergency
  unauthenticated filesystem writes.
- Serial recovery remains the ultimate fallback for a firmware fault.

## Read-only API compatibility with current HACS

HACS v0.1.0-dev14 currently *requires* `rollback_support`,
`previous_revision_sha256`, `nvs_readable`, `slot0_valid`,
`slot1_valid`, `selector_initialized`, `selector_sequence`, and
`generation_activation_enabled` as structured fields in its **read-only**
capability/status parser. To avoid breaking an existing installation, preview
firmware temporarily returns `rollback_support=false`, an empty
`previous_revision_sha256`, false selector flags and zero selector sequence.

These are **deprecated compatibility placeholders**, not evidence of active
A/B logic. HACS issue #2 must eventually replace them with an explicit
`storage_model=single-active`, `active_revision_sha256`, positive
authenticated transaction support, and recovery-mode state; never use the
preview metadata to enable deployment. HACS code is **not modified** here.

## Remaining firmware and hardware gates

- [x] Old A/B generation selector, NVS boot selector, trial/rollback code
      removed from this experimental branch and host regression list.
- [x] Existing staged-object, canonical manifest, digest, dependency validation
      and one-shot local approval foundations retained and host tested.
- [x] Duplicate full 16 MiB flash reads and saved SHA-256 checksum verified
      by the user; offline partition inspection / restore still not proven.
- [ ] A single current-revision persistence scheme, power-failure-safe atomic
      activation, and immutable runtime path resolver wired into *all* readers.
- [ ] Self-contained local recovery UI + provisioning/reconnect behavior when
      scene files are corrupt; verify without HA/Wi-Fi.
- [ ] First-time secure pairing UI, encrypted authenticated transport, replay
      protection, admin/target confirmation integration.
- [ ] Begin/chunk/validate/commit/abort/status HTTP protocol with exact schema,
      conflict semantics, capacity limits and stable error responses.
- [ ] Verify flash/heap/LVGL responsiveness, reset/power-cut safety, malformed
      configuration recovery and physical reconnect/redeploy on Waveshare.
- [ ] Update HACS expectations and deployment UI only after the firmware
      contract is frozen and hardware recovery is demonstrated.

**Safety:** Until all necessary controls are proven,
`transaction_support=false`, `rollback_support=false` and all remote
configuration mutation routes remain disabled. No merge to the working branch.
