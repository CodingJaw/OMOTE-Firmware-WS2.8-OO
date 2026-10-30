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
