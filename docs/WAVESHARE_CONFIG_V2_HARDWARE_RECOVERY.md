# WAVESHARE Config v2 — backup and safe hardware acceptance

> **Design update (November 1, 2026):** A/B configuration switching and
> automatic rollback have been retired. Follow
> [CONFIG_V2_SINGLE_ACTIVE_DESIGN.md](CONFIG_V2_SINGLE_ACTIVE_DESIGN.md)
> for the current single-active design. The A/B-specific tests listed further
> below are historical and must not be attempted.

**Scope:** Only physical Waveshare ESP32-S3-Touch-LCD-2.8 V2, the existing
16 MiB-flash configuration, and the experimental branch
`feature/config-v2-transactional`. This procedure does not merge firmware,
enable HACS deployments or modify LittleFS contents.

## Current test constraints (user confirmed)

- Only **one** Waveshare, currently functional.
- **User reports** the full-flash double read and saved SHA-256 checksum both passed. This is an integrity-verified backup, **not** a proven restore. The offline layout inspector below provides an additional check.
- No battery installed; USB disconnect fully removes device power.
- Touchscreen/scenes/local UI can be tested now; IR/BLE hardware tests are not
  currently available.
- HACS `main` / `develop` remains read-only for configuration deployments.
- Device remains functional without Wi-Fi or Home Assistant.

**Gate:** Do not attempt actual A/B pointer selection, power-cut fault injection,
automatic rollback, filesystem formatting, `uploadfs`, or experimental HACS
mutation until a verifiable recovery backup is produced. Normal compile,
host-only tests and read-only diagnostics are allowed.

## Backup (read-only; no erase/write commands)

Connect by USB to Debian, close any serial monitor and run from repository root:

```bash
git switch feature/config-v2-transactional
git pull --ff-only
cd Platformio
bash tools/config-v2-tests/backup-waveshare-full-flash.sh /dev/ttyACM0
```

The script uses `~/.platformio/penv/bin/python -m esptool` to read address
`0x0` through `0x1000000` (16 MiB) **twice**, compares them byte-for-byte,
and writes a SHA-256 checksum under `~/omote-backups/`. It never writes
device flash. If flash encryption or readout restrictions are enabled,
`esptool` may fail or produce ciphertext, and a matching readback alone does
not prove the image is restorable. Review those conditions before attempting
restoration.

Backups contain firmware, LittleFS and potentially NVS Wi-Fi/HA/FTP/MQTT
credentials. They must be stored securely **outside the Git repository**.
No automatic restore command is supplied; restoration is destructive and
requires a separate, deliberate board-specific check.

## Additional offline backup-layout verification (no serial access required)

The user reports that the two complete reads match and the checksum check
returned `OK`. Check that the file's boot headers and partition table also
match the known Waveshare firmware layout:

```bash
cd Platformio
BACKUP="$(ls -td ~/omote-backups/waveshare28v2-* | head -1)"
python3 tools/config-v2-tests/inspect-waveshare-backup.py \
  "$BACKUP/waveshare-esp32s3-16mb-full-flash.bin"
```

Expected message:

```text
PASS: saved SHA-256 checksum and expected 16 MiB boot/partition layout.
```

The inspection performs a **read-only** streaming SHA-256 comparison and
checks ESP image headers and the exact expected partition entries:
NVS `0x9000/0x5000`, OTA data `0xe000/0x2000`, app `0x10000/0xa00000`,
LittleFS partition `0xa10000/0x5e0000`, coredump `0xff0000/0x10000`.
It never prints firmware bytes, passwords or LittleFS content.

The inspector deliberately rejects unreadable/encrypted/unknown image layouts
rather than treating them as verified. It cannot prove that the backup will
boot after restoration. Do not restore the image to the sole working board
merely to verify this checker.

**Next test gate:** Once the offline layout check passes, confirm serial
access, retain a known-good firmware build/source, check `Scenes.json`
availability using existing read-only API, and check standalone UI after
disconnecting Wi-Fi. Before any deliberate power-cut fault injection, the
operator must still have a reviewed manual restore procedure; actual recovery
remains unproven until hardware restoration succeeds.

## Safe regression testing (no configuration activation)

```bash
bash tools/config-v2-tests/run-host-tests.sh
~/.platformio/penv/bin/platformio run -e waveshare28v2_omote
```

After a successful build and backup, flash ONLY under a controlled USB test,
without `uploadfs`. Check normal scene navigation, touch response and
behavior after Wi-Fi loss. The preview API reports non-deployable capabilities
and must keep `generation_activation_enabled=false`.

## Future destructive/fault-injection tests — BLOCKED until recovery is proven

- Unplug USB while writing a *staged* inactive object, validate active JSON.
- Unplug during single active-manifest publication; verify either the old
  intact active pointer or the new fully validated pointer is visible.
- Force a malformed/missing scene dependency; verify the independent local
  recovery UI and secure HA redeployment rather than automatic rollback.
- Unplug during an authenticated commit, inspect status and safely retry with
  the same request ID without unintentional duplicate activation.
- Test too little LittleFS space, stale revision, invalid credentials, token
  replay, broken active scene and recovery-mode reconnect.
- Measure touch latency and free heap throughout; Wi-Fi/HA outage alone must
  never cause rollback.

The first real power cut is a **separate hardware acceptance event** after
backup/restore verification, not an automatic CI step.
