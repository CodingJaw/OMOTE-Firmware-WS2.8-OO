# WAVESHARE Config v2 — backup and safe hardware acceptance

**Scope:** Only physical Waveshare ESP32-S3-Touch-LCD-2.8 V2, the existing
16 MiB-flash configuration, and the experimental branch
`feature/config-v2-transactional`. This procedure does not merge firmware,
enable HACS deployments or modify LittleFS contents.

## Current test constraints (user confirmed)

- Only **one** Waveshare, currently functional.
- **No verified flash backup** yet.
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

**Next test gate:** Once backup comparison succeeds, first test a manual
non-destructive recovery path (serial access, known-good firmware source,
`Scenes.json` presence and offline UI) before any deliberate power-cut test.

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
- Unplug during manifest publication and boot selector update, then boot the
  previously confirmed generation.
- Force a malformed/missing scene dependency, verify local rollback.
- Unplug during an authenticated commit, retry with the same request ID and
  verify exact-once semantics.
- Test too little LittleFS space, stale revision, invalid credentials, token
  replay, and repeated boot resets.
- Measure touch latency and free heap throughout; Wi-Fi/HA outage alone must
  never cause rollback.

The first real power cut is a **separate hardware acceptance event** after
backup/restore verification, not an automatic CI step.
