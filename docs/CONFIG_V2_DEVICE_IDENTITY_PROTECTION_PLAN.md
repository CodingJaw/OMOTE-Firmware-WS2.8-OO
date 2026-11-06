# OMOTE device identity protection gate — Waveshare ESP32-S3 V2

**Status: design and verification gate only. No provisioning, no eFuse writes,
no production pairing, no firmware upload and no configuration deployment.**

## Observed starting point

- The isolated `waveshare28v2_omote_sec2_lab` target uses mixed
  Arduino/ESP-IDF 5.3.2 with the Waveshare ESP32-S3-Touch-LCD-2.8 V2.
- The checked-in `sdkconfig.defaults.waveshare28v2` enables Security2
  support, but does **not** explicitly enable `CONFIG_NVS_ENCRYPTION`,
  Secure Boot, flash encryption, or an HMAC eFuse key-protection scheme.
  This alone is not a runtime eFuse/security-status measurement; the actual
  built `.pio/build/.../config/sdkconfig.h` and the device's read-only
  eFuse status require separate inspection.
- The tested lab P-256 private signing key is a software scalar in a
  versioned `Preferences` NVS record `omsec2id/labkey`. It is
  copyable from unencrypted storage, and its DER/hex public fingerprint
  is only an untrusted **lab** device identifier.
- Home Assistant dev22 has a per-remote candidate P-256 key in HA local
  `.storage` (also NOT encrypted at rest by this integration). It is
  explicitly administrator-prepared and persists across ordinary HA
  restarts, but has NO connection to the firmware production pairing API.

## Production threat model

An attacker may control local LAN/mDNS/IP advertisements, replay prior
HTTP requests, impersonate another firmware remote, replace a Home Assistant
installation, copy an unencrypted flash image or Home Assistant backup,
interrupt power during enrollment, or induce a firmware rollback. Production
pairing may not rely on an unencrypted NVS scalar, a six-digit PIN alone,
a MAC/IP address, a previous lab socket, or a self-reported capability JSON.

Physical compromise with unrestricted runtime code execution is a different
security boundary than encrypted storage at rest. HMAC-backed NVS encryption
protects stored key confidentiality under defined conditions, but a
software signing key is still accessed by the firmware in RAM. Therefore
**NVS encryption is not, by itself, a non-exportable signing key**.

## Two candidate protection architectures (no action selected)

### A. ESP32-S3 HMAC eFuse-derived NVS encryption

- ESP-IDF provides HMAC-backed NVS encryption on ESP32-S3, deriving the
  storage encryption keys from a device eFuse HMAC key. It requires
  explicit compatible config and an eFuse key provisioned for the HMAC
  purpose. The eFuse action is irreversible and may affect recovery.
- The existing `Preferences` record is **not automatically upgraded** by
  enabling an encrypted NVS backend. Its migration, key loss recovery,
  and fail-closed behavior must be tested on sacrificial hardware first.
- Secure Boot, firmware signature/rollback controls, debug and download
  restrictions may be needed in the threat model; changing them alters
  the user's existing USB upload and recovery workflow. A key usable
  by general firmware remains a software key, not a hardware signature
  operation. Any production trust based on this option requires an
  explicit risk acceptance.

### B. External secure element / device-held non-exportable P-256 key

- A supported secure element capable of P-256 ECDSA can keep the
  private signing key non-exportable and expose only a public key and
  signature operation through its secure interface.
- Hardware selection, I2C wiring, provisioning, recovery/replaceability,
  counterfeit/supply-chain considerations, and attestation policy must
  be tested; an I2C bus does not itself authenticate an external chip.
- This may better satisfy a non-exportable P-256 device identity, but
  it adds PCB, firmware driver and enrollment complexity.

Do not select either option implicitly or enable automatically because
`sdkconfig` advertises an available crypto feature.

## Required acceptance gates before real HACS pairing

1. Collect **read-only** ESP32-S3 eFuse/security state and actual generated
   build config. Save a verified full flash backup and document a known-good
   USB recovery route before changing security fuses.
2. Select an explicit device-key protection architecture and recovery
   threat model. **Do not burn any eFuse without separate human approval.**
3. Provision a unique persistent device private key without exporting
   plaintext to the network, logs, firmware repo or OMOTE config JSON.
   Verify that a second device cannot clone its identity by flash copy.
4. Bind candidate HA public key and protected device public key to one
   live, mutually authenticated Security2/transcript session; require
   an explicit current touchscreen review and compare the full HA key
   fingerprint before committing trust.
5. Persist exact authorized HA public key and versioned generation with
   power-loss consistency and readback. Failure or tampering must block
   all protected commands. No silently regenerated trust or downgrade.
6. After reboot, verify fresh challenge-based **PIN-less** reconnection
   from the saved HA private key, with device fingerprint pinning and
   replay protection. Fresh HA install requires fresh physical pairing;
   imported backups require explicit identity-recovery policy.
7. Keep configuration deployment, OTA, privileged API routes and
   recovery resend disabled until independently authenticated admin
   and physical transaction approvals, integrity-checked backup,
   hardware rollback, and power-loss testing all pass.

## Safe source references

- ESP-IDF NVS encryption and HMAC key protection:
  https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/api-reference/storage/nvs_encryption.html
- ESP32-S3 flash encryption and secure boot implications:
  https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/security/flash-encryption.html
- Official ESP-IDF HMAC NVS example:
  https://github.com/espressif/esp-idf/tree/v5.3.2/examples/security/nvs_encryption_hmac

Until an architecture is deliberately chosen and hardware-validated,
the security gate remains CLOSED regardless of lab test success.
