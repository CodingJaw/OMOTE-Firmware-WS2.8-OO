#!/usr/bin/env python3
"""Capture raw ESP32 receipt-LAB serial output without PlatformIO monitor.

Read-only application protocol: this script never calls Serial.write().
Opening a serial port can toggle USB control lines on some hardware.
After capture begins, press RESET once to produce a complete boot log.

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
    try:
        # No serial.write: we only receive diagnostic bytes, display and save.
        # Opening USB serial may toggle DTR/RTS; manual RESET is recommended.
        with serial.Serial(port=port, baudrate=baud, timeout=0.2) as source:
            print("Serial capture started. Press the ESP32 RESET button once.",
                  flush=True)
            deadline = time.monotonic() + seconds
            with output.open("wb") as log:
                while time.monotonic() < deadline:
                    received = source.read(4096)
                    if received:
                        log.write(received)
                        log.flush()
                        sys.stdout.write(received.decode("utf-8", errors="replace"))
                        sys.stdout.flush()
    except (OSError, ValueError, serial.SerialException) as exc:
        print(f"ERROR: serial capture failed: {exc}", file=sys.stderr)
        return 2
    print(f"\nCapture saved: {output}", flush=True)
    return 0


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
