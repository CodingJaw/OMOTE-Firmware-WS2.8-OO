# Waveshare ESP32-S3-Touch-LCD-2.8 V2 — OMOTE port

This is a **staged port**, not yet the full OMOTE firmware.

## Stage 1 (this branch)

The `waveshare28v2` PlatformIO target runs a hardware-validation screen, using
the same known-working pin assignments and CST3530 protocol as
[CodingJaw/lvgl-wsesp32s32.8](https://github.com/CodingJaw/lvgl-wsesp32s32.8)
branch `waveshare-v2-showcase`.

Hardware:
- 240x320 ST7789 over SPI: SCK=40, MOSI=45, CS=42, DC=41, RST=39
- Backlight: GPIO5 (20 kHz 8-bit PWM, initially 50%)
- CST3530 V2 touch on **Wire1**: SDA=1, SCL=3, RST=2, INT=4, I2C=0x58
- ESP32-S3, 16 MB flash, OPI PSRAM

### Build and upload

Open the `Platformio/` folder:

```bash
~/.platformio/penv/bin/platformio run -e waveshare28v2
~/.platformio/penv/bin/platformio run -e waveshare28v2 -t upload
~/.platformio/penv/bin/platformio device monitor -b 115200
```

Expected: board detection on serial, a full-screen LVGL diagnostic UI, and
a button that increments when the screen is touched. This only validates the
driver and basic touch; it does not exercise OMOTE remote functionality.

### Important safeguards

- Do not enable `OMOTE_HARDWARE_REV5` or use its keyboard/IR/battery pin
  assignments on the Waveshare.
- The V1 CST328 touch protocol is **not** interchangeable with CST3530 V2.
- `waveshare28v2` currently compiles the standalone bring-up source only.
  OMOTE's normal UI and services are **not yet wired into this target**.
- `dev`, `waveshare-merged`, `x64_sim`, and the original OMOTE Rev1/Rev5
  build configurations remain unchanged.

## Next port steps

1. Confirm the diagnostic display, touch coordinates and button taps on the device.
2. Implement a Waveshare-specific `HardwareAbstract` and `DisplayAbstract`
   adapter so OMOTE's normal `OmoteSetup` and `JsonUI` can run.
3. Keep keypad, IR, battery and sleep functionality disabled or explicitly
   mapped until the actual remote hardware is wired and tested.
4. Test the full target for boot, LittleFS assets, web configurator, navigation
   and touch-to-sleep transitions before adding external peripherals.


## Stage 2 — experimental real OMOTE JsonUI

A separate `waveshare28v2_omote` PlatformIO environment is available. It boots
the actual `OMOTE::setup()` and `UI::JsonUI`, selecting the
`WaveshareHardware` implementation of OMOTE's `HardwareAbstract` interface.
This is **experimental and not yet compile-tested on hardware**.

The adapter reuses the Stage 1 ST7789 and CST3530 drivers and the existing
OMOTE ESP32 Wi-Fi, JSON configuration, UI and WebSocket libraries.
Unsupported hardware is explicitly inert: IR transmission and receive, physical
keypad, IMU, fuel-gauge and sleep operations are disabled. Battery readings
shown by the interface are placeholders, not measurements.

### Build workflow

```bash
# From the cloned repository root
git switch waveshare28v2-port
git pull --ff-only
cd Platformio
~/.platformio/penv/bin/platformio run -e waveshare28v2_omote
```

Fix build errors before uploading. The full UI has additional dependencies
relative to Stage 1; use the same mixed Arduino + ESP-IDF framework as OMOTE
Rev5 rather than forcing the isolated Arduino-only diagnostic environment.

### Filesystem (mandatory for JSON scenes)

Stage 2 uses OMOTE's 16 MB `Rev5Partitions.csv` and expects scene/page/device
JSON under the `data/` folder on LittleFS.

**Important:** Switching between Stage 1 and Stage 2 changes the partition
layout. Back up anything in flash you need before uploading. Avoid formatting
LittleFS automatically; the Waveshare adapter attempts a nonformatting mount.

After successful compilation, upload the filesystem and the firmware while the
`waveshare28v2_omote` environment is selected:

```bash
~/.platformio/penv/bin/platformio run -e waveshare28v2_omote -t uploadfs
~/.platformio/penv/bin/platformio run -e waveshare28v2_omote -t upload
~/.platformio/penv/bin/platformio device monitor -b 115200
```

A successful boot should print `[waveshare28v2] Starting real OMOTE JsonUI`,
a LittleFS mount/scene check, then the OMOTE JSON home screen should load its
scenes. Scene selections and on-screen buttons are expected to render, but
IR actions cannot actuate hardware. Wi-Fi and web configuration need separate
functional validation and are not guaranteed by a successful compile.

To return to the verified touchscreen test, rebuild and upload
`-e waveshare28v2`.

### Required follow-up validation

- Build/link the experimental environment and correct any compiler errors.
- Verify flash partition, LittleFS scene loading, screen rendering and touch.
- Check navigation and settings without unsafe casts or hardware GPIO access.
- Verify Wi-Fi/Web Configurator and Home Assistant only after base UI is stable.
- Add TCA8418, IR and power systems using measured pins on the actual remote.
