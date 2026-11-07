#!/usr/bin/env python3
"""Capture raw ESP32 receipt-LAB serial output without PlatformIO monitor.

Read-only application protocol: this script never calls Serial.write().
Opening a serial port can toggle USB control lines on some hardware.
The updated diagnostic repeats results every three seconds, so reset is unnecessary.\nIf USB disconnects/re-enumerates, the capture retries opening the port.

Usage from Platformio:
  ~/.platformio/penv/bin/python tools/config-v2-tests/capture_receipt_device_serial.py \
      --port /dev/ttyACM0 --output receipt-device-serial.log --seconds 25

Then run check_receipt_device_serial.py on the saved log.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys
import time


def capture(port: str, output: Path, seconds: float, baud: int = 115200) -> int:
    # Import at runtime so unit tests and offline parser work without pyserial.
    try:
        import serial
    except ImportError:
        print("ERROR: pyserial is required (use PlatformIO's Python environment).",
              file=sys.stderr)
        return 2
    if not port.startswith("/dev/"):
        print("ERROR: explicit /dev/... serial port required.", file=sys.stderr)
        return 2
    if not (0.5 <= seconds <= 120):
        print("ERROR: capture duration must be between 0.5 and 120 seconds.",
              file=sys.stderr)
        return 2
    if not (1200 <= baud <= 2000000):
        print("ERROR: unsupported baud rate.", file=sys.stderr)
        return 2
    if output.exists() and not output.is_file():
        print("ERROR: output target is not a regular file.", file=sys.stderr)
        return 2
    # Opening the port may reset some USB CDC adapters. The firmware now
    # repeats a complete result every three seconds; never manually reset.
    # Read-only at the device protocol level: no source.write calls.
    deadline = time.monotonic() + seconds
    captured = bytearray()
    complete = False
    connected_once = False
    last_error = ""
    try:
        with output.open("wb") as log:
            while time.monotonic() < deadline and not complete:
                try:
                    with serial.Serial(port=port, baudrate=baud, timeout=0.2) as source:
                        if not connected_once:
                            print("Connected. No reset required; waiting for repeating receipt results.",
                                  flush=True)
                        else:
                            print("Serial port reconnected.", flush=True)
                        connected_once = True
                        while time.monotonic() < deadline:
                            chunk = source.read(4096)
                            if not chunk:
                                continue
                            log.write(chunk)
                            log.flush()
                            captured.extend(chunk)
                            sys.stdout.write(chunk.decode("utf-8", errors="replace"))
                            sys.stdout.flush()
                            # A final diagnostic line is emitted after each
                            # complete crypto check. Return at its newline
                            # boundary so a second, partial cycle does not
                            # invalidate the final parser check.
                            for line in captured.splitlines(keepends=True):
                                if (line.endswith(b"\\n") and
                                    b"[RECEIPT-LAB] FINAL " in line and
                                    b"[RECEIPT-LAB] isolated firmware mbedTLS offline vector verification" in captured):
                                    complete = True
                                    break
                            if complete:
                                break
                            # Prevent unbounded memory growth with a noisy
                            # serial console. The file remains the full log.
                            if len(captured) > 16384:
                                del captured[:-8192]
                except (OSError, ValueError, serial.SerialException) as exc:
                    last_error = str(exc)
                    # The port may disappear while the S3 re-enumerates.
                    # Retry opening it until the overall timeout expires.
                    time.sleep(0.25)
    except (OSError, ValueError) as exc:
        print(f"ERROR: serial log output failed: {exc}", file=sys.stderr)
        return 2
    print(f"\\nCapture saved: {output}", flush=True)
    if complete:
        return 0
    print(f"ERROR: no complete receipt report received; last port error: {last_error or 'none'}",
          file=sys.stderr)
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--output", type=Path,
                        default=Path("receipt-device-serial.log"))
    parser.add_argument("--seconds", type=float, default=25)
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()
    return capture(args.port, args.output, args.seconds, args.baud)


if __name__ == "__main__":
    raise SystemExit(main())
