#!/usr/bin/env bash
# Non-destructive, double-read backup for Waveshare ESP32-S3 16 MiB flash.
# Usage: bash tools/config-v2-tests/backup-waveshare-full-flash.sh [serial-port] [backup-directory]
# DO NOT upload output: it can contain Wi-Fi, MQTT and Home Assistant secrets.
set -euo pipefail
umask 077
port="${1:-/dev/ttyACM0}"
backup_root="${2:-$HOME/omote-backups}"
python="${PYTHON:-$HOME/.platformio/penv/bin/python}"
if [[ ! -x "$python" ]]; then
  echo "PlatformIO Python not found: $python" >&2
  exit 2
fi
if ! esptool_help="$("$python" -m esptool --help 2>/dev/null)"; then
  echo "esptool module unavailable in $python; install it in the PlatformIO environment." >&2
  exit 2
fi
# esptool v5 documents 'read-flash'; earlier releases use 'read_flash'.
flash_read_cmd="read_flash"
if grep -q "read-flash" <<< "$esptool_help"; then
  flash_read_cmd="read-flash"
fi
if [[ ! -e "$port" ]]; then
  echo "Serial device missing: $port" >&2
  exit 2
fi
mkdir -p -- "$backup_root"
chmod 700 "$backup_root"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
dir="$backup_root/waveshare28v2-$stamp"
mkdir -m 700 -- "$dir"
a="$dir/waveshare-esp32s3-16mb-full-flash.bin"
b="$dir/verification-second-read.bin"
echo "Reading complete 16 MiB flash twice (NO WRITE/ERASE)."
echo "Close PlatformIO serial monitor and any other programs using $port."
"$python" -m esptool --chip esp32s3 --port "$port" --baud 460800 \
  "$flash_read_cmd" 0x0 0x1000000 "$a"
test "$(stat -c %s "$a")" -eq 16777216 || {
  echo "Wrong backup size; backup not verified." >&2
  exit 3
}
"$python" -m esptool --chip esp32s3 --port "$port" --baud 460800 \
  "$flash_read_cmd" 0x0 0x1000000 "$b"
test "$(stat -c %s "$b")" -eq 16777216 || {
  echo "Second read had wrong size; backup not verified." >&2
  exit 3
}
if ! cmp -s "$a" "$b"; then
  echo "BACKUP INVALID: two full-flash reads differ. Do not start recovery tests." >&2
  exit 4
fi
rm -- "$b"
(
  cd -- "$dir"
  sha256sum waveshare-esp32s3-16mb-full-flash.bin \
    > waveshare-esp32s3-16mb-full-flash.sha256
)
cat > "$dir/README-PRIVATE.txt" <<EOF
Device: Waveshare ESP32-S3-Touch-LCD-2.8 V2, expected 16 MiB flash
Serial port at capture: $port
Backup UTC: $stamp
Verification: TWO full-flash reads compared byte-for-byte; PASS
This backup can include Wi-Fi passwords, API tokens and other private data.
Keep private; never commit to GitHub or include in support logs.
A successful backup read does not prove that restoring it will work.
Before restore, verify chip identity, flash security state, partition map,
firmware build and recovery procedure. Restore is MANUAL ONLY.
EOF
echo "SUCCESS: duplicate full flash reads match."
echo "Backup folder: $dir"
echo "Checksum:"
cat "$dir/waveshare-esp32s3-16mb-full-flash.sha256"
echo "No data was written to the device."
