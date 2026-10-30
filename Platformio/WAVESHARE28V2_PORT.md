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


### Arduino SPI framework-cache mismatch (October 2026)

The mixed Arduino/ESP-IDF environment uses **Tasmota platform
2025.03.30**, which expects the Tasmota Arduino core **3.1.3.250302**.
The environment now pins that release's framework package explicitly.

If you see `SPI.cpp: spiFrequencyToClockDiv(_freq)` failing because
`esp32-hal-spi.h` expects `spiFrequencyToClockDiv(spi_t*, uint32_t)`,
you have a cross-version package/cache mismatch: the older SPI sources
and a newer core header have been mixed. Editing the toolchain's SPI.cpp
is not a durable fix.

From the repository root, pull the latest branch. From `Platformio/`,
first rebuild the target using the pinned package:

```bash
~/.platformio/penv/bin/platformio run -e waveshare28v2_omote
```

The setup script now checks for the incompatible header **before**
spending time on compilation. If it still reports a mismatched global
framework package, do not alter your shared PlatformIO packages used
by your other ESP32 projects. Instead, build with an isolated PlatformIO
core directory, which installs a separate set of frameworks:

```bash
PLATFORMIO_CORE_DIR="$HOME/.platformio-omote" \
  ~/.platformio/penv/bin/platformio run -e waveshare28v2_omote
```

This isolation downloads tools/frameworks again, but avoids changing
the global packages used by your proven Waveshare showcase.

The board has **16 MB flash**. A previous build's ESP-IDF cache may
retain an incorrect 4 MB sdkconfig despite the board settings. This
target now uses `sdkconfig.defaults.waveshare28v2` and explicitly
specifies 16 MB. If the 4 MB warning persists, remove **only**
`Platformio/.pio/build/waveshare28v2_omote` and rebuild; do not
erase user data on the ESP32 merely to clear a compile-time cache.

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

### Fix for stale ESP-IDF 4 MB configuration

The upstream checked-in `Platformio/sdkconfig.defaults` is a generated
configuration for the **original ESP32** and explicitly selects 4 MB flash.
The Waveshare target uses a separate `sdkconfig.defaults.waveshare28v2`
that selects **16 MB**. ESP-IDF may keep an existing generated
`sdkconfig.waveshare28v2_omote` with the old flash setting: defaults do
not override previously set values in the generated configuration.

After pulling changes, from `Platformio/`:

```bash
# Inspect the effective generated config before changing anything.
grep -E '^CONFIG_(IDF_TARGET|ESPTOOLPY_FLASHSIZE)' \
  sdkconfig.waveshare28v2_omote 2>/dev/null || true

# Move aside ONLY the generated configuration of the experimental target.
if test -f sdkconfig.waveshare28v2_omote; then
  mv sdkconfig.waveshare28v2_omote sdkconfig.waveshare28v2_omote.bak
fi

# Reset ONLY the target's cached CMake and compilation outputs.
rm -rf .pio/build/waveshare28v2_omote
~/.platformio/penv/bin/platformio run -e waveshare28v2_omote
```

Do **not** delete `sdkconfig.defaults`, change `Rev5Partitions.csv`,
or erase the board just to fix this warning. The hardware's flash was
already reported as 16 MB by the platform; the 4 MB message is an ESP-IDF
configuration discrepancy. This is separate from source/compiler errors.


### PSRAM and generic DevKit board label

PlatformIO labels the generic `esp32-s3-devkitc-1` variant "N8 (8 MB QD,
No PSRAM)", even when its flash and memory type settings are overridden.
That label does **not** probe the actual Waveshare board. Waveshare's
ESP32-S3-Touch-LCD-2.8 V2 has **16 MB flash and 8 MB octal PSRAM**.

The `waveshare28v2_omote` target inherits `board_build.psram_type=opi`,
`board_build.arduino.memory_type=qio_opi` and `BOARD_HAS_PSRAM` from
`esp32_Rev5`. Its `sdkconfig.defaults.waveshare28v2` now also
explicitly enables `CONFIG_SPIRAM=y`, octal PSRAM, 80 MHz and allocation
through `malloc()`. Simply defining `BOARD_HAS_PSRAM` is not sufficient
for a mixed Arduino + ESP-IDF build if IDF external-memory support is off.

After compilation, the next firmware boot will report:
`[waveshare28v2] Flash size: ... bytes, PSRAM total: ... bytes, PSRAM free: ... bytes`.
Expected total PSRAM is approximately **8,388,608 bytes**. A zero total
indicates PSRAM has not initialized, regardless of the generic board label.

**On rebuild**, inspect the generated `sdkconfig.waveshare28v2_omote`.
If an old configuration has disabled PSRAM, move that generated file aside
and delete only `.pio/build/waveshare28v2_omote`, as described above.
Do not alter the generic ESP32 board definition or the working diagnostic
environment.

### mDNS compiler include error

ESP-IDF's mDNS component is vendored at
`components/espressif__mdns/include/mdns.h`. The Arduino framework's
`ESPmDNS` and `ArduinoOTA` libraries compile separately and need its
include directory explicitly; the Waveshare OMOTE target now adds
`-I components/espressif__mdns/include` alongside the WebSocket
component include directory. This avoids modifying third-party framework
headers and is limited to the experimental target.


### Stage 2: LittleFS recovery and boot diagnostics

When flashing Stage 2 after the standalone diagnostic target, an existing
`spiffs` data partition may be uninitialized or contain an incompatible
filesystem. ESP-IDF then reports errors such as
`Corrupted dir pair at {0x0, 0x1}` or `mount failed (-84)`.
**Do not infer the reset cause from this message alone.** Examine the
reset reason and any exception/backtrace from a complete 115200 baud log.
A mount error may appear **twice**: LVGL 9.4's Arduino LittleFS driver
calls `LittleFS.begin()` during `lv_init()`, then the Waveshare adapter
calls `LittleFS.begin(false)` to check for the scene manifest. The
Arduino core 3.1.3 `begin()` default is `formatOnFail=false`; neither
attempt automatically formats the partition.

The Waveshare startup now checks `LittleFS.begin(false)` and requires
`/Scenes.json`. If unavailable, it does **not** erase or automatically
format flash; it displays a static storage-recovery screen and continues
servicing the Wi-Fi stack/captive portal instead of constructing the full
JSON UI without valid assets. Startup logs include reset reason and
initialization stages.

The `waveshare28v2_omote` build uses `Rev5Partitions.csv`. Its
`spiffs`-named partition is used by **LittleFS** because the target
selects `board_build.filesystem = littlefs`. Upload the `data/`
directory as a dedicated filesystem image, using the **same target**:

```bash
# From Platformio/ after pulling this branch and compiling successfully
~/.platformio/penv/bin/platformio run -e waveshare28v2_omote -t uploadfs
~/.platformio/penv/bin/platformio run -e waveshare28v2_omote -t upload
~/.platformio/penv/bin/platformio device monitor -p /dev/ttyACM0 -b 115200
```

**CAUTION:** `uploadfs` replaces the contents of the configured LittleFS
partition. Back up any needed data first. It does **not** erase the
entire 16 MB flash, but it is destructive **to that partition**.

A successful boot should log that LittleFS mounted and `Scenes.json`
was found. If rebooting persists, capture the full serial output from
the `rst:` / `ESP-ROM` boot line through any `Guru Meditation`,
`Backtrace`, `abort`, or subsequent reset.

This experimental target no longer inherits the Rev5 LIS3DH, LTR329/
LTR303, or MAX1704x dependencies. Its dormant LIS3DH translation unit is
also excluded at compile time; no corresponding device initialization
should run.
