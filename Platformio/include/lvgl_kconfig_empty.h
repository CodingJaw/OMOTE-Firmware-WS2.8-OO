#pragma once
// Pure Arduino Waveshare target: LVGL configuration is supplied through
// build_flags, not ESP-IDF Kconfig. Avoid pulling ESP-IDF's esp_attr.h
// into non-ESP-IDF assembly compilation units (e.g. Helium .S files).
